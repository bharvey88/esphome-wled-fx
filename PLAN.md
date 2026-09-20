# esphome-wled-fx: plan (single source of truth for this port)

Goal: port every WLED 16.0.1 effect (1D, 2D, particle, audio-reactive) plus the licence-clean WLED-MM exclusives to ESPHome. Ship first as an external component in this repo, then upstream to esphome/esphome in staged PRs.

Read before working: `recon/wled-inventory.md`, `recon/wled-mm-inventory.md`, `recon/esphome-surface.md`. Reference sources (read-only): `refs/WLED` (v16.0.1), `refs/WLED-MM` (mdev), `refs/esphome` (dev).

## Hard rules
- No em dashes in any text. No tool or assistant credit lines of any kind, and no Co-Authored-By lines. Commit author email `8107750+bharvey88@users.noreply.github.com`. Multiline commit messages go through `git commit -F <ascii file>`.
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
   - Effect registry: each effect is `void mode_xxx(Segment &seg)` plus a `const EffectInfo {name, fn, flags(1D/2D/audio/particle), default speed/intensity/custom/palette parsed from WLED metadata}`. Effects are keyed by NAME (WLED and MM numeric IDs diverge). Effects are grouped in separate translation units (`effects/fx_1d_a.cpp`, ...), each exporting one registration table, so parallel contributors never edit the same file. Linker garbage collection plus a YAML `effects:` allow-list (codegen defines) keeps flash small: only selected effect groups or names get compiled in. Default = all.
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
- P1 Foundation (one contributor, sequential): repo skeleton, licence, engine, math, palettes, registry, both front ends, host sim, 10 representative effects (Solid, Blink, Rainbow, Fire 2012, Pride 2015, Noise 1, Plasma, 2D Matrix, 2D Metaballs, 2D Scrolling Text), examples, README, `PORTING.md` (the exact mechanical transform rules and a checklist for effect porters), CI workflow that runs the host sim and esphome compile. Commit and push to a new public repo `bharvey88/esphome-wled-fx`.
- P2 Effect batches (parallel contributors, one translation unit each, non-audio, non-particle): 1D batches A to D (about 30 effects each), 2D batch, 1D+2D batch.
- P3 Particle system engine, then particle 2D effects and particle 1D effects.
- P4 Audio source + FFT, then the 37 audio-reactive effects (8 of them particle-based, after P3).
- P5 MM exclusives: GEQ 3D, Paintbrush, Snow Fall, Party jerk, Multi Comet audio, Popcorn/Starburst/Fireworks audio, Meteor Smooth; note MM differences for GEQ / Game of Life as options only where cheap.
- P6 Review pass, docs (README effect table generated from the registry), tag a release.
- P7 Upstream: fork esphome/esphome under bharvey88, branch with engine + light front end + a small effect set under the 1000 net non-test line guidance, tests under `tests/components/wled_fx/`, clang-tidy clean, PR description drafted per the upstream PR template plus the esphome.io docs PR. The staged follow-up list is written to `UPSTREAM.md`.

## Status log
(appended as each phase lands)

### 2026-09-19, integration of the first parallel wave

Merged ten branches into `main` with ordinary merge commits: the six non-particle
effect batches (`fx_1d_b`, `fx_1d_c`, `fx_1d_d`, `fx_1d_e`, `fx_2d`, `fx_1d2d`),
the two non-particle audio batches (`fx_audio_vol`, `fx_audio_fft`), the particle
system engine (`particle_engine`) and the audio source (`audio_source`). Every
worktree was clean at merge time. The only conflict in the whole set was the
deviations list in `PORTING.md`, where the particle and audio branches both
numbered their entries 14 to 16; both sets are kept and the audio ones are now 17
to 19. All ten worktrees and branches are removed.

186 effects registered, which is the expected 10 + 30 + 24 + 30 + 24 + 29 + 7 +
16 + 13 + 3 particle smoke tests.

Helpers the porters had to duplicate, because they were forbidden from editing
engine files, are now in the engine once each, in the new
`components/wled_fx/wf_fx_shared.h` and `.cpp` plus `wf_color.h`, `wf_math.h` and
`wf_segment.h`. `SPEED_FORMULA_L` became an inline function. The batches
disagreed about `FAIR_DATA_PER_SEG`; it is 2048 everywhere now, which is what
upstream's `MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS` computes on a plain ESP32.

Two engine-side bugs fixed. Derived identifiers are now unique, so "Sparkle" and
"Sparkle+" no longer share a macro and a PNG name, and the simulator fails if any
two registered effects ever collide again. The simulator also gained three
non-square geometries, a per-effect pacing table (Sunrise only), and a `--map`
that applies only to effects that can run in 1D, which was corrupting the heap on
Game Of Life.

Verified on merged `main`: 1116 simulator runs with zero failures at every
geometry and in every one of the five mapping modes, guards intact; 83 audio
checks with zero failures; `esphome compile` green for all four example configs.

Still to do in P3 to P5: 10 of the 12 particle 2D effects, 10 of the 11 particle
1D effects, the 8 audio-particle effects and the 9 WLED-MM exclusives.

### 2026-09-19, integration of the final wave: the port is complete

Merged the last four branches into `main` with ordinary merge commits:
`fx_particle_2d`, `fx_particle_1d`, `fx_audio_particle` and `fx_mm`. Every
worktree was clean and every tip matched its hand-off. There were no conflicts at
all: three branches touch one new effect file each and the fourth adds a licence
subsection to `README.md`. All four worktrees and branches are removed.

**223 effects registered**, which is the expected 186 + 10 + 10 + 8 + 9. That is
all 216 WLED 16.0.1 effects except Image and Copy Segment, which need a
filesystem and a multi-segment model this component does not have, plus the nine
WLED-MM exclusives. `BATCHES.md` is now all merged.

Consolidation. The only helper genuinely defined in two effect files was the
`SPOT_TYPE_*` spotlight set, shared by Dancing Shadows and PS Dancing Shadows; it
is in `wf_fx_shared.h` once. `wf_effects_mm.cpp`'s `map8()`, `map2()`,
`draw_line_depth()` and the Snow Fall helpers stay local, because the engine has
no equivalent and only that file uses them, and the `particle` / `star` struct
that `1d_e` and `mm` both declare stays duplicated because the MM copy belongs to
an intentionally copied MM core.

Three correctness items closed:

1. `Segment::has_real_audio()` is the one place that answers "is a microphone
   attached", which is a different question from "is there audio data", because
   `seg.audio()` always has a frame. PS Attractor now uses it instead of poking
   at `audio_source()`, and PS Spray and PS Blobs get upstream's non-audio
   animation back; both had dropped the branch as unreachable.
2. PS Sonic Stream clamps the particle index it keeps in `seg.aux1`. Upstream
   never does, and the index can outlive a smaller particle system.
3. PS Springy's spring force region checks out against the allocator, both the
   size and the alignment, and the effect now bounds-checks it against
   `seg.data_size()` rather than trusting a chain of invariants that live in
   another file.

The simulator gained `--check1..3`, `--custom1..3` and `--checks-on`, and its
default run now makes a second pass over every effect with all three checkmarks
on. That pass was the only thing reaching PS Pinball's rolling and collide modes,
PS Springy's AR mode and the Cylinder, Collide and Gravity options across the
particle effects. Capture frames scale with the run length, and PS Galaxy is in
the pacing table at 1500 frames so its arms have formed before the last tile.

Two findings came out of the new pass, both faithful to upstream rather than port
bugs, and both are recorded in the simulator rather than worked around. Sparkle
Dark, Sparkle+ and Snow Fall render black with all checks on, because check2 is
an Overlay flag that suppresses the background and the secondary colour defaults
to black; the non-black assertion is therefore only a failure in the default
pass. PS Sonic Boom renders nothing below 21 pixels, because its per-beat
particle count rounds to zero there, which a 16x16 panel under `M12_P_BAR`
reaches; `BLACK_ALLOWED` forgives it with that length bound and no wider.

Verified on merged `main`, all of it locally on this machine. MinGW's
`collect2.exe` is quarantined by Windows Defender, but the WinLibs GCC 16.1.0
toolchain in `PATH` links fine, so nothing had to be deferred to CI:

* 2676 simulator runs at all six geometries across both control passes, zero
  failures.
* 13380 runs across `--map` 0 to 4, both control passes, zero failures. CI runs
  that sweep with `--single-pass`, 6690 runs, because doubling it as well took
  the sanitizer job past an hour; run it in full before a release.
* 83 audio checks, zero failures.
* All 223 metadata strings present in `firmware.factory.bin`.
* `esphome compile` green for all four example configs.

| Config | Flash | Free in a 4 MB app slot | RAM |
|---|---:|---:|---:|
| `m1-hub75.yaml` | 940,555 | 894,453 (48.7%) | 109,931 (32.2%) |
| `m1-hub75-audio.yaml` | 994,879 | 840,129 (45.8%) | 118,851 (34.8%) |
| `strip-esp32.yaml` | 932,147 | 902,861 (49.2%) | 46,668 (25.8%) |
| `strip-esp32-arduino.yaml` | 1,019,819 | 815,189 (44.4%) | 47,820 (26.5%) |

`HARDWARE-CHECKLIST.md` is new and collects every "worth a look on real
hardware" note from the porters and from `PORTING.md`, grouped by what has to be
flashed to check it. Nothing in this repository has ever been flashed.

P2 to P5 are done. What is left is P6, the review pass and a tagged release, and
P7, the staged upstream PRs to esphome/esphome.
