# esphome-wled-fx: plan (single source of truth for all agents)

Goal: port every WLED 16.0.1 effect (1D, 2D, particle, audio-reactive) plus the licence-clean WLED-MM exclusives to ESPHome. Ship first as an external component in this repo, then upstream to esphome/esphome in staged PRs.

Read before working: `recon/wled-inventory.md`, `recon/wled-mm-inventory.md`, `recon/esphome-surface.md`. Reference sources (read-only): `refs/WLED` (v16.0.1), `refs/WLED-MM` (mdev), `refs/esphome` (dev).

## Hard rules
- No em dashes in any text. No Claude/AI credit or Co-Authored-By lines anywhere. Commit author email `8107750+bharvey88@users.noreply.github.com`. Multiline commit messages go through `git commit -F <ascii file>`.
- Never flash hardware, never run `esphome run/upload`, never probe serial ports. Compile and validate only.
- `esphome compile` runs from PowerShell using `C:\Users\bharv\esphome-venv` (never Git Bash), with configs under a short path (for example `C:\tmp\wfx\`). If that venv's esphome lacks something needed (hub75), create a second venv with esphome dev at `C:\Users\bharv\esphome-dev-venv` using Python 3.13; do not modify the existing venv.
- Licence: repo is GPL-3.0-or-later. Every file derived from WLED carries a header: original file, original authors, "EUPL-1.2-or-later, distributed here under GPLv3 per EUPL Article 5 compatibility clause". `fastled_slim` derived code keeps its MIT notice. No WLED-derived data tables in Python files (ESPHome Python is MIT); palettes, names and metadata live in C++.
- Excluded: animartrix (CC BY-NC), ARTI-FX, IMU games. GEQ 3D and Paintbrush are allowed (GPLv3), note their licence in the header.
- No FastLED, no Arduino dependency. Must build on ESP32 with esp-idf framework (hub75 is IDF only) and with arduino, and on the `host` platform.

## Architecture
Component name `wled_fx`, namespace `esphome::wled_fx`, at `components/wled_fx/`.

1. Engine (framework-free C++, no ESPHome includes beyond helpers/log):
   - `Canvas`: owns a `uint32_t` (0x00RRGGBB, W in top byte where used) framebuffer of width x height, allocated once at setup with `RAMAllocator` (PSRAM preferred). Gives lossless `get_pixel`, so effects that read back work on every output. 1D is height 1. Optional serpentine / pixel mapper applied at output time, not in effects.
   - `Segment`: the effect context. Same field names as WLED (`speed`, `intensity`, `custom1..3`, `check1..3`, `aux0`, `aux1`, `step`, `call`, `palette`, `colors[3]`, `data`, `allocate_data()`), plus `now`, `length()`, `width()`, `height()`, `is_2d()`, and the drawing helpers (`set_pixel_color`, `set_pixel_color_xy`, `get_pixel_color(_xy)`, `fill`, `fade_to_black_by`, `fade_out`, `blur`, `blur2d`, `blend_pixel_color`, `add_pixel_color`, `move`, `draw_line`, `draw_circle`, `fill_circle`, `draw_character`, `color_from_palette`, `color_wheel`, ...). ESPHome naming style (snake_case methods, trailing underscore privates). Effect scratch data is allocated on effect start and freed on effect change, never per frame.
   - `math/`: port of `fastled_slim` and WLED util math (sin8, cos8, beatsin8/16/88, scale8, qadd8, perlin8/16 1D-3D, hw random wrappers over `random_uint32`, triwave, cubicwave, ease, gamma off). No `#define` constants; use `constexpr` / `static const`.
   - `palettes`: all WLED gradient palettes + FastLED built-ins + the dynamic ones (random cycle, from colors) in C++ PROGMEM-style const arrays.
   - Effect registry: each effect is `void mode_xxx(Segment &seg)` plus a `const EffectInfo {name, fn, flags(1D/2D/audio/particle), default speed/intensity/custom/palette parsed from WLED metadata}`. Effects are keyed by NAME (WLED and MM numeric IDs diverge). Effects are grouped in separate translation units (`effects/fx_1d_a.cpp`, ...), each exporting one registration table, so parallel agents never edit the same file. Linker garbage collection plus a YAML `effects:` allow-list (codegen defines) keeps flash small: only selected effect groups or names get compiled in. Default = all.
   - Effect bodies stay as close to WLED as possible (mechanical transform: `SEGMENT.`/`SEGENV.` to `seg.`, `SEGLEN` to a local `const`, `strip.now` to `seg.now`, `hw_random*` to the math wrappers), so future WLED updates can be diffed. No macros.
   - WLED gamma is OFF inside the engine. The light front end relies on ESPHome's gamma/colour correction; the display front end has its own optional `gamma` option.
2. Particle system: port of upstream 16.0.1 `FXparticleSystem` (2D and 1D) against `Segment`/`Canvas`, in `particle/`. Not the older MM snapshot.
3. Audio: `audio/` holds an `AudioSource` that takes an ESPHome `microphone` (pattern copied from `sound_level`: MicrophoneSource + ring buffer), runs FFT on a task using `espressif/esp-dsp` (already pinned by ESPHome, added with `add_idf_component`), and produces the WLED `um_data` contract (volumeSmth, volumeRaw, fftResult[16], samplePeak, FFT_MajorPeak, my_magnitude, ...) with AGC. When no microphone is configured, audio effects use WLED's simulated sound so they still run. Audio effects read a plain struct, not `um_data` void pointers.
4. Front ends:
   - Light: addressable light effect `wled_fx:` registered through `register_addressable_effect`. Options: `effect` (name), `speed`, `intensity`, `custom1..3`, `check1..3`, `palette`, `width`/`height`/`serpentine` for matrices, `update_interval`. Primary colour comes from the light's current colour. Self-gated frame timing (WLED FRAMETIME, 42 fps target).
   - Display: `wled_fx:` top-level component with `display_id`, renders the canvas with ONE `draw_pixels_at` per frame from `loop()`, and triggers the display flip itself (hub75 only flips inside `update()`, so set the display `update_interval: never` and call its update, confirm in hub75 source). Runtime control: actions `wled_fx.set_effect` etc, plus optional `select` (effect, palette) and `number` (speed, intensity, custom1..3) and `switch` (check1..3) platforms so Home Assistant can drive it like WLED.
5. Host simulator (`tools/sim/`): builds the engine natively (CMake or a plain script, whatever toolchain exists on this Windows box; zig cc, clang, MSVC or WSL are all acceptable, find what is installed), renders every registered effect for 300 frames at 16x16, 64x64 and 1x60, asserts no crash, no out-of-bounds write (guard bands around the framebuffer) and non-black output, and can dump a PNG/GIF contact sheet per effect for visual review. This is the main verification while the user is away from hardware. Also run with ASan/UBSan if the toolchain supports it.

## Verification gates (every phase)
1. Host sim passes for all effects registered so far.
2. `esphome compile` passes for: (a) `examples/m1-hub75.yaml` (ESP32-S3, esp-idf, hub75 board preset `apollo-automation-m1-rev6`, display front end), (b) `examples/strip-esp32.yaml` (ESP32, addressable strip via esp32_rmt_led_strip, light front end, both arduino and esp-idf), (c) `examples/host.yaml` if practical.
3. Report flash and RAM numbers.

## Phases
- P1 Foundation (one agent, sequential): repo skeleton, licence, engine, math, palettes, registry, both front ends, host sim, 10 representative effects (Solid, Blink, Rainbow, Fire 2012, Pride 2015, Noise 1, Plasma, 2D Matrix, 2D Metaballs, 2D Scrolling Text), examples, README, `PORTING.md` (the exact mechanical transform rules and a checklist for effect porters), CI workflow that runs the host sim and esphome compile. Commit and push to a new public repo `bharvey88/esphome-wled-fx`.
- P2 Effect batches (parallel agents, one translation unit each, non-audio, non-particle): 1D batches A to D (about 30 effects each), 2D batch, 1D+2D batch.
- P3 Particle system engine, then particle 2D effects and particle 1D effects.
- P4 Audio source + FFT, then the 37 audio-reactive effects (8 of them particle-based, after P3).
- P5 MM exclusives: GEQ 3D, Paintbrush, Snow Fall, Party jerk, Multi Comet audio, Popcorn/Starburst/Fireworks audio, Meteor Smooth; note MM differences for GEQ / Game of Life as options only where cheap.
- P6 Review pass (code review agents), docs (README effect table generated from the registry), tag a release.
- P7 Upstream: fork esphome/esphome under bharvey88, branch with engine + light front end + a small effect set under the 1000 net non-test line guidance, tests under `tests/components/wled_fx/`, clang-tidy clean, PR description drafted per the upstream PR template plus the esphome.io docs PR. The staged follow-up list is written to `UPSTREAM.md`.

## Status log
(orchestrator appends here)
