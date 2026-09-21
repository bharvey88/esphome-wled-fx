# Porting a WLED effect to wled_fx

This is the working document for anyone porting effects. With this page plus the
engine headers you should not need to read anything else, and you should not need
to change any engine file.

Source of truth for the effects: WLED **v16.0.1**, `wled00/FX.cpp`
(commit `29b389df1c1aaec6ff53aea742d17063b985906c`). Keep the bodies as close to
upstream as you can, so a future WLED release can be diffed against them.

---

## 1. The rules that matter most

1. **One translation unit per porter.** Your batch's `components/wled_fx/wf_effects_<group>.cpp`
   already exists, empty, and nobody else touches it. You never edit an engine
   file, another effect file, `__init__.py`, or the simulator. `BATCHES.md` says
   which file is yours and which effects go in it.
2. **Copy the body, do not rewrite it.** Keep upstream's variable names, comment
   text, brace style and even its odd casing. The transform in section 3 is
   mechanical and nothing else should change.
3. **Copy the metadata string verbatim.** It carries the display name, the slider
   labels, the dimensionality flags and the defaults, and it is parsed at runtime.
4. **Keep the credits.** If the upstream effect has a comment naming its author,
   that comment comes with it.
5. **No heap allocation per frame.** `seg.allocate_data()` is a no-op when the size
   has not changed, so calling it every frame the way WLED does is fine; anything
   else is not.
6. **Nothing from WLED goes into a `.py` file.** The Python side is MIT and must
   stay independent work. Names, tables and defaults live in C++.

## 2. File skeleton

```cpp
/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

#include "wf_effects.h"

// Whole-file guard. List every effect this file provides.
#define WLED_FX_GROUP_1D_B \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE || WLED_FX_FX_SPARKLE)

#if WLED_FX_GROUP_1D_B

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE
void mode_twinkle(Segment &seg) {
  ...
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE
    {"Twinkle@!,!;!,!;!;;m12=0", mode_twinkle},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_B;
const EffectGroup EFFECT_GROUP_1D_B{"1d_b", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_B
```

Your file currently holds the empty form of that, because a zero length array is
not valid C++:

```cpp
const EffectInfo *const ENTRIES = nullptr;
constexpr size_t ENTRY_COUNT = 0;
```

Replace those two lines with the array form above as soon as you add your first
effect, and add each `WLED_FX_FX_*` macro to the group guard at the top as you go.

Conventions the build depends on, so get them exactly right:

| Thing | Rule |
|---|---|
| File name | `wf_effects_<group>.cpp` in `components/wled_fx/` |
| Group guard | one `#define WLED_FX_GROUP_<ID>` listing every `WLED_FX_FX_*` the file provides |
| Group object | `EFFECT_GROUP_<ID>`, same `<ID>`, with a preceding `extern` declaration |
| Per-effect macro | `WLED_FX_FX_` + the display name uppercased, with `+` `&` `/` `#` `%` `*` expanded to `_PLUS` `_AND` `_SLASH` `_HASH` `_PCT` `_STAR` and every remaining run of non-alphanumeric characters turned into one `_` |

The effect macro is what a user's `effects:` allow-list turns into, so "Fire 2012"
becomes `WLED_FX_FX_FIRE_2012` and "Colorwaves Pride" becomes
`WLED_FX_FX_COLORWAVES_PRIDE`. The punctuation expansion exists because the plain
rule made "Sparkle" and "Sparkle+" the same macro, so naming one in YAML pulled in
both. `Sparkle+` is `WLED_FX_FX_SPARKLE_PLUS`. The rule lives in three places that
have to agree, `effect_macro()` in `components/wled_fx/__init__.py` and
`effect_macro()` and `sanitize()` in `tools/sim/main.cpp`, and the simulator fails
on startup if any two registered effects derive the same macro or the same output
file name, so you find out immediately.

Both ESPHome codegen and the simulator's CMake read those two lines to build the
table of groups that got compiled in. They do it by scanning the source, which is
why the spelling is fixed: there is no shared list anywhere for you to update.

## 3. The transform table

| WLED 16.0.1 | wled_fx | Notes |
|---|---|---|
| `void mode_x()` | `void mode_x(Segment &seg)` | every effect takes the segment |
| `SEGMENT.` / `SEGENV.` | `seg.` | the two were already identical upstream |
| `SEGLEN` | `seg_len` | add `const unsigned seg_len = seg.length();` as the first line |
| `SEG_W` / `SEG_H` | `seg.width()` / `seg.height()` | or local `cols` / `rows` as upstream often does |
| `SEGCOLOR(n)` | `seg.color(n)` | |
| `SEGPALETTE` | `seg.palette_ref()` | |
| `PALETTE_SOLID_WRAP` | `seg.palette_solid_wrap()` | |
| `PALETTE_MOVING_WRAP` | `seg.palette_moving_wrap()` | |
| `strip.now` | `seg.now` | same timestamp for every effect in a frame |
| `millis()` | `seg.now` | never read the clock yourself |
| `micros()` | `seg.now_us` | the same instant in microseconds, so it steps 1000 at a time |
| `strip.isMatrix` | `seg.is_2d()` | see the pitfall in section 6 |
| `SEGMENT.is2D()` | `seg.is_2d()` | |
| `FRAMETIME` | `FRAMETIME` | a `constexpr`, not a macro |
| `SEGMENT.setPixelColor` | `seg.set_pixel_color` | and the same snake_case rule for every other helper |
| `setPixelColorXY` | `set_pixel_color_xy` | |
| `fadeToBlackBy` | `fade_to_black_by` | |
| `fadeToSecondaryBy` | `fade_to_secondary_by` | |
| `fade_out` / `fill` / `blur` | unchanged | already snake_case |
| `blur2D` / `blurRows` / `blurCols` | `blur2d` / `blur_rows` / `blur_cols` | |
| `moveX` / `moveY` / `move` | `move_x` / `move_y` / `move` | |
| `drawCircle` / `fillCircle` / `drawLine` | `draw_circle` / `fill_circle` / `draw_line` | |
| `wu_pixel` | `wu_pixel` | unchanged |
| `color_from_palette` / `color_wheel` | unchanged | |
| `SEGENV.allocateData(n)` | `seg.allocate_data(n)` | |
| `SEGENV.data` | `seg.data` | `uint8_t *` |
| `SEGENV.dataSize()` | `seg.data_size()` | |
| `nrOfVStrips()` | `seg.nr_of_v_strips()` | |
| `indexToVStrip(i, n)` | `Segment::index_to_v_strip(i, n)` | static |
| `hw_random*` / `perlin*` / `sin8_t` … | unchanged | in `wf_math.h` |
| `random8(...)` / `random16(...)` | `hw_random8(...)` / `hw_random16(...)` | a few WLED-MM bodies still use FastLED's names; the bounds are exclusive-upper on both, so the call site behaves the same |
| `beatsin8_t(bpm, lo, hi)` | `beatsin8_t(bpm, lo, hi, seg.now)` | **the clock is an argument now**, see below |
| `beat8(bpm)` / `beat16` / `beat88` | `beat8(bpm, seg.now)` … | same |
| `map(...)` | `wf_map(...)` | Arduino's `map` does not exist here |
| `MIN(a,b)` / `MAX(a,b)` / `min` / `max` | a ternary, or `std::min` / `std::max` | the macros are gone |
| `constrain(x, lo, hi)` | `constrain(x, lo, hi)` | a function template in `wf_math.h`, mixed argument types are fine |
| `radians(d)` / `degrees(r)` | unchanged | `constexpr` in `wf_math.h` |
| `sin_t` / `cos_t` / `tan_t` | unchanged | in `wf_math.h`, over `sin_approx` and friends |
| `inoise8` / `inoise16` | `perlin8` / `perlin16` | upstream's own guidance is not to use the legacy aliases |
| `bitRead/bitSet/bitClear(v, n)` | `v & (1 << n)` / `v \|= (1 << n)` / `v &= ~(1 << n)` | Arduino macros, gone |
| `bitWrite(v, n, b)` | `b ? (v \|= 1 << n) : (v &= ~(1 << n))` | same |
| `pgm_read_byte_near(p)` / `pgm_read_byte` | unchanged | plain loads in `wf_math.h`; there is no separate program address space |
| `PSTR("x")` / `F("x")` | `"x"` | drop the wrapper, and `strncmp_P` / `sprintf_P` lose the `_P` |
| `getAudioData()` and the `um_data` casts | `seg.audio()` | see section 7 |
| `strip.getBrightness()` | drop the term | the light front end owns brightness; treat it as 255 |
| `strip.getCurrSegmentId()` | `0` | one canvas, one segment |
| `strip.getActiveSegmentsNum()` / `getSegmentsNum()` | `strip_active_segments_num()` | one canvas, one segment, so it returns 1 |
| `strip.getMaxSegments()` | `strip_max_segments()` | **not 1.** It is how many segments the build allows, 64 with PSRAM and 32 without, and effects compare the active count against a fraction of it. Writing 1 turns `segs <= (getMaxSegments() / 2)` into `1 <= 0` and silently quarters the effect's scratch budget |
| `SEGMENT.vWidth()` / `vHeight()` / `vLength()` | `seg.width()` / `seg.height()` / `seg.length()` | same |
| `FX_FALLBACK_STATIC` | `FX_FALLBACK_STATIC` | still a macro, fills with colour 0 and returns |
| `gamma8` / `gamma8inv` / `gamma32` / `gamma32inv` | unchanged | real lookup tables at WLED's default gamma of 2.2, see section 6 |
| `CRGB`, `CHSV`, `CRGBW`, `CHSV32`, `CRGBPalette16` | unchanged | in `esphome::wled_fx`, not the global namespace |
| `RGBW32(r,g,b,w)`, `R(c)`, `G(c)`, `B(c)`, `W(c)` | unchanged | namespaced `constexpr` functions, not macros |
| `BLACK`, `WHITE`, `RED`, … | unchanged | `constexpr uint32_t` |
| `struct virtualStrip { static void runStrip(...) }` | a `const auto run_strip = [&](...)` lambda | see Fire 2012 in `wf_effects_1d_a.cpp` |
| a stack VLA | a fixed array, or `seg.allocate_data()` | VLAs are not allowed |

### The beat functions

WLED's `beatsin8_t(...)` reads `millis()` internally. Here the frame timestamp is
an explicit argument so the whole frame is coherent and the host simulator is
reproducible. The `timebase` and `phase_offset` arguments keep their upstream
positions after it:

```
WLED:    beatsin16_t(bpm, lo, hi, timebase, phase)
wled_fx: beatsin16_t(bpm, lo, hi, seg.now, timebase, phase)
```

If upstream passed no timebase, just append `seg.now`.

## 4. Reading the metadata string

Copy it verbatim. `effect_defaults()` in `wf_registry.h` parses it at runtime, so
you do not transcribe any of it into C++ fields. Layout, semicolon separated:

```
Name@slider0,slider1,slider2,slider3,slider4,check1,check2,check3;color0,color1,color2;palette;flags;defaults
```

* Everything before the first `@` is the display name, and the name is the key the
  registry, the YAML and the select entity all use. Numeric WLED IDs are not used
  anywhere, because WLED and WLED-MM disagree about them.
* The palette group: a leading digit is the default palette ID.
* The flags group: `0` means 0D capable, `1` 1D, `2` 2D, `v` volume reactive, `f`
  FFT reactive. Missing or empty means 1D.
* The **last** group is the defaults list, read as `key=value` pairs:
  `sx` speed, `ix` intensity, `c1` / `c2` / `c3` custom 1 to 3, `o1` / `o2` / `o3`
  the checkmarks, `pal` palette, `m12` the 1D-to-2D mapping, `si` the sound
  simulation type. WLED reads the last `;` group, not group index 4, and so does
  this parser, which matters for the handful of malformed strings upstream.
* Anything the string does not set keeps the engine default (speed 128, intensity
  128, custom1/2 128, custom3 16, checks off, palette 0).
* Defaults are only applied to controls the user did not pin in YAML or at runtime.

The first three groups are not only decoration: `effect_labels()` reads them for
the control names, and the `wled_fx` text sensor publishes them. Get them right.

* An empty slider, checkmark or colour label means the effect does not use that
  control, and it is left out of the published line entirely. Do not write a
  plausible name for a control your effect ignores.
* `!` means WLED's own name for it: Speed, Intensity, Custom 1 to 3, Check 1 to
  3 for the controls, `Fx`, `Bg` and `Cs` for the three colour slots and
  `Color palette` for the palette.
* A numeric palette group is a pinned palette, and the selector is hidden, so it
  is not published as a control either.

`custom3` is five bits, 0 to 31, everywhere: in the YAML schema, in the number
platform, in the `wled_fx.set_custom3` action and in `Engine::set_custom3()`,
which clamps. Effects divide it down on that assumption, so a c3 default above
31 is a bug and `wled_fx_effect_test` fails on one.

## 5. Running the simulator

Build once:

```
cmake -S tools/sim -B tools/sim/build -G Ninja
cmake --build tools/sim/build
```

Then, for just your translation unit:

```
tools/sim/build/wled_fx_sim --group 1d_b --out tools/sim/out
```

or for one effect:

```
tools/sim/build/wled_fx_sim --effect "Twinkle" --out tools/sim/out
```

Other options: `--frames N` (default 300), `--palette N` to override the palette,
`--no-images` for a text-only run, `--list` to dump the parsed defaults of every
effect so you can check your metadata string was read the way you expected.

`--speed`, `--intensity` and `--text STRING` are the other three. Reach for them
when somebody reports an effect misbehaving on hardware: a control left over
from the previous effect is what most of those reports turn out to be, and these
reproduce that state here rather than guessing at it.

`--check1`, `--check2` and `--check3` take 0 or 1, `--custom1` to `--custom3` take
0 to 255, and `--checks-on` is shorthand for all three checkmarks. They pin the
control the way the YAML options do, so they survive the effect defaults being
applied. With none of them given, the simulator runs every effect **twice** per
geometry, once on its own metadata defaults and once with all three checkmarks on;
the second pass is the only thing that reaches the alternative mode most effects
hide behind a checkbox, and its contact sheets get a `_checks` suffix. Naming any
control collapses that to the single configuration you asked for, written with a
`_cli` suffix so an experiment cannot overwrite the default run's sheets.
`--single-pass` drops the checks pass and leaves the defaults alone; CI uses it
for the five `--map` runs, because doubling those as well multiplied the
sanitizer job's time by about five for very little extra reach. Run the mapping
sweep without it before a release.

Two tables at the top of `tools/sim/main.cpp` hold the per-effect exceptions, and
both want a comment saying why:

* `PACING` gives an effect a longer run or a longer frame period. Sunrise needs
  12 s per frame because its default speed is a 60 minute sunrise; PS Galaxy needs
  1500 frames because its arms only form after several hundred frames of spiral
  motion, and that motion is per frame rather than clock driven. Capture frames
  are scaled to the run length, so a longer run still gets its last tile near the
  end.
* `BLACK_ALLOWED` forgives the non-black assertion below a given virtual strip
  length. It has one entry, PS Sonic Boom, whose per-beat particle count rounds to
  zero below 21 pixels. The length bound is the point: the assertion stays live at
  every normal size.

The non-black assertion only fails in the default pass. Several effects put an
Overlay checkmark on `check2`, which tells them not to paint a background at all,
and the secondary colour defaults to black, so an all-black checks pass is the
correct result for Sparkle Dark, Sparkle+ and Snow Fall. The checks pass exists
for the guard bands and for reaching the code, so it reports black rather than
failing on it. Guard bands are checked in every pass.

Every effect is run at 16x16, 64x64, 60x1, 64x32, 32x64 and 31x17. The two
transposed matrices catch an effect that mixes up its axes, and 31x17 is odd in
both dimensions so nothing can quietly rely on a power of two. The run fails if
the canvas guard bands were overwritten, if every frame came out black, or if the
PNG could not be written. **Look at the PNGs.** "Not black" is a very low bar and
it will not catch an effect that is subtly wrong.

An effect paced against wall-clock time can need more than the seven seconds that
300 frames at 23 ms covers. Rather than run it for hundreds of thousands of
frames, add it to the `PACING` table at the top of `tools/sim/main.cpp` with a
longer frame period. Sunrise is the worked example: its default speed is a 60
minute sunrise, so it gets 12 s per frame. Nothing in the effect changes, because
an effect only ever reads `seg.now`.

`--map N` overrides the 1D-to-2D mapping mode (`m12`), 0 to 4, which is the only way
to see an effect through a mapping its metadata does not select. Use it on any 1D
effect you port, at 64x64, to check nothing writes out of bounds in a mapping the
default never exercises. It is ignored for effects that are 2D only, because `m12`
is the *1D to 2D* mapping and WLED only offers it on 1D effects. Forcing it onto a
2D-native effect breaks an assumption upstream shares: Game Of Life sizes its
allocation from `seg.length()`, which under `M12_P_BAR` is the matrix height and
not width times height.

Add `-DWLED_FX_SANITIZE=ON` at configure time for an ASan and UBSan build. That
works with GCC or Clang on Linux; MinGW on Windows does not ship those runtimes, so
on Windows the guard bands are the bounds check you get.

Before you open a pull request, also compile at least one example:

```
esphome compile examples/strip-esp32.yaml
```

### On Windows: run it in WSL

MinGW is not a good place to run this. It has no sanitizers, and on at least one
machine Windows Defender has quarantined its linker outright, which leaves the
simulator unbuildable with nothing obviously wrong. WSL2 has the whole toolchain,
a real ASan and UBSan, and is where the ESPHome host platform works too, so the
Linux build is the one to trust.

[tools/wsl](tools/wsl) is that build behind one PowerShell command:

```
powershell -File tools\wsl\wfx.ps1 bootstrap
powershell -File tools\wsl\wfx.ps1 sweep
```

`bootstrap` installs the toolchain and builds a virtualenv with ESPHome's dev
branch in it, which the snapshot harness needs; it checks each step and is safe
to run again. `sweep` is the whole of what CI runs: every effect at every
geometry under the sanitizers, the small geometries, the five mapping modes, the
audio and behaviour tests, then the large geometries without the sanitizers, and
the two Python checks. It takes a few minutes on sixteen cores.

The other commands are `build`, `run` (everything after it goes to
`wled_fx_sim`, so `wfx.ps1 run --effect "Fire 2012" --size 64x64` works),
`audio`, `effect`, `snapshot`, `shell` and `clean`.

The repository is not copied anywhere. WSL reads it at `/mnt/c/...` and only the
build directories and the virtualenv live on the Linux filesystem, under
`/root/wfx`, because a build directory on `/mnt/c` crosses the filesystem bridge
for every object file and is several times slower. An edit made on Windows is
picked up with no sync step.

Two things to know if you are working in there by hand. `wsl.exe` writes UTF-16,
so reading its output from PowerShell means stripping the NULs, which the
wrapper does. And WSL will happily run a Windows `.exe` through binfmt interop,
so `tools/check_effect_names.py` would pick up a stale `wled_fx_sim.exe` sitting
in the checkout: set `WLED_FX_SIM` to the Linux binary, as `sim.sh` does.

### When the simulator is not enough

The simulator runs `Engine::render()`. It cannot see a bug in the ESPHome
display front end, because there is no ESPHome in it, and the front end had two
of them: the frame was handed to `draw_pixels_at()` with the wrong endianness,
which swapped red and blue on every display except the one it had been tried
on, and the default primary colour was the wrong amber.

[tools/snapshot](tools/snapshot) is the answer to that. It is an ESPHome `host`
build over the `snapshot` display, so a captured frame has been through the
YAML, codegen, the frame gate, `draw_pixels_at()` and the display's own
`update()`. [tools/compare](tools/compare) puts its output next to a capture of
a real WLED device and next to the plain simulator, which is what turns "this
differs" into "and here is which half of the stack it is in".

If you are porting an effect, the simulator is still the loop to work in. Reach
for these when an effect looks right in a contact sheet and wrong on hardware.

## 6. Pitfalls hit while building P1

* **A `const` object at namespace scope has internal linkage.** This bit twice:
  once on `EFFECT_GROUP_*` and once on the generated table. It is also why the
  original design, a self-registering object chained into a linked list, does not
  work at all inside ESPHome: the generated sources are built into a static
  library, and a translation unit nothing names is never pulled out of the archive,
  so its static initialiser never runs. The build looks perfectly healthy and the
  effect simply is not there. If you ever suspect this, check for your metadata
  string in the firmware image with the `strings` check in section 8.
* **There are two gammas in WLED and only one of them is yours.** The output
  stage, `gamma32()` on the finished frame inside `show()`, belongs to the ESPHome
  light layer's `gamma_correct` or to the display front end's own option, and the
  engine does not do it. The other one is the gamma maths effect and particle code
  does *while drawing*, to pre-compensate a brightness so the output stage lands
  where the effect author wanted it. That is part of the picture, it shows up in
  WLED's own pre-output buffer, and `gamma8`, `gamma8inv`, `gamma32` and
  `gamma32inv` are real lookup tables here at WLED's default gamma of 2.2. Copy
  the calls as upstream writes them and expect them to do something. This was
  wrong until v0.3.1 and it cost twenty particle effects between 15 and 57 percent
  of their brightness. The output stage was still an identity by default until
  v0.4.0, which cost the same effects the other half of the round trip: the
  display front end now defaults to 2.2, which is what WLED's `show()` uses.
  Deviation 33 has the numbers and what a hub75 panel and an ESPHome light each
  do with them.
* **`strip.isMatrix` has no equivalent.** Upstream distinguishes "the whole strip
  is a matrix" from "this segment happens to be 2D". Here there is one canvas and
  one segment, so both become `seg.is_2d()`. Every `if (!strip.isMatrix || !SEGMENT.is2D())`
  collapses to `if (!seg.is_2d())`.
* **Palette 0 means "use the segment colours", not "use a nice default".** That is
  faithful WLED behaviour and it makes palette-driven effects render flat. Metaballs
  with the default palette is a solid orange field with three white dots on both
  WLED and here. Check such effects with `--palette 50` before deciding they are
  broken.
* **`seg.length()` depends on the 1D-to-2D mapping.** With `m12=1` (`M12_P_BAR`) on
  a matrix it is the matrix *height*, and the effect is run once per column through
  the virtual strip index. Fire 2012 is the worked example.
* **Division by `seg_len`.** Guard it, or make sure the division only happens inside
  a loop that cannot run when the length is zero. A 1-pixel light is a real
  configuration.
* **`wf_map` is not Arduino's `map`.** It returns `long` and it returns `out_min`
  when the input range is empty, instead of dividing by zero.
* **Arduino turns half the helper names into macros.** `constrain`, `radians`,
  `degrees`, `min`, `max`, `abs` and the `pgm_read_*` family are all `#define`s
  under the Arduino framework, and a macro eats a function of the same name before
  the compiler sees it. The definitions in `wf_math.h` are guarded with `#ifndef`
  for exactly that reason, and Arduino's versions are arithmetically identical, so
  a call site behaves the same either way. If you ever add a helper of your own,
  check the name is not an Arduino macro, and compile
  `examples/strip-esp32-arduino.yaml` as well: the esp-idf build will not catch it.
* **Effect functions must be in the anonymous namespace.** Two files porting the
  same shared base function (`chase`, `ripple_base`, `twinklefox_base` …) would
  otherwise collide at link time. Duplicating a shared base into two files is fine
  and expected; do not try to share one between files.

## 7. Audio effects

Upstream hands an effect a `um_data_t`, a tagged array of void pointers that every
audio effect indexes and casts by hand:

```cpp
um_data_t *um_data = getAudioData();
float volumeSmth   = *(float*)   um_data->u_data[0];
uint8_t *fftResult =  (uint8_t*) um_data->u_data[2];
```

Here that whole preamble collapses to one line, and the field names are the same
words in snake case:

```cpp
AudioData &audio = seg.audio();
// then audio.volume_smth, audio.fft_result[...], and so on
```

| WLED | wled_fx | Type |
|---|---|---|
| `u_data[0]` `volumeSmth` | `audio.volume_smth` | `float` |
| `u_data[1]` `volumeRaw` | `audio.volume_raw` | `uint16_t`, cast where upstream reads it signed |
| `u_data[2]` `fftResult` | `audio.fft_result` | `uint8_t[16]` |
| `u_data[3]` `samplePeak` | `audio.sample_peak` | `uint8_t` |
| `u_data[4]` `FFT_MajorPeak` | `audio.fft_major_peak` | `float` |
| `u_data[5]` `my_magnitude` | `audio.my_magnitude` | `float` |
| `u_data[6]` `maxVol` | `audio.max_vol` | `uint8_t`, written back by the effect |
| `u_data[7]` `binNum` | `audio.bin_num` | `uint8_t`, written back by the effect |
| MM `u_data[8..11]` | `fft_major_peak_smth`, `sound_pressure`, `agc_sensitivity`, `zero_crossing_count` | only the `fx_mm` batch needs these |
| `NUM_GEQ_CHANNELS`, `MAX_FREQUENCY`, `MAX_FREQ_LOG10` | unchanged | `constexpr` in `wf_audio.h` |

`seg.audio()` returns a **non-const** reference, because a few effects write
`max_vol` and `bin_num` back from their own sliders, exactly as upstream does.

With no microphone configured, `seg.audio()` returns a port of WLED's
`simulateSound()`, driven by `seg.now` and picked by the metadata key `si`, so
every audio effect animates and renders in the simulator. All four of upstream's
simulation modes are copied line for line with one exception, the beat: see
deviation 27. The simulated frame is
generated once per timestamp, so all the effects in a frame see one coherent set
of numbers.

A real microphone is configured with an `audio:` block on the `wled_fx`
component, documented in the README. Nothing in an effect body changes either
way: `seg.audio()` returns the live analysis when a source is attached and has
data, and the simulation otherwise.

### "Is a real microphone attached?"

A handful of effects call `UsermodManager::getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE)`
not to obtain the data, which they could get anyway, but to ask whether audio is
real, and they run a **different animation** when it is not. `seg.audio()` cannot
answer that: it always returns a frame. Use `seg.has_real_audio()` and keep both
of upstream's branches:

```cpp
if (seg.has_real_audio()) {  // get AR data, do not use simulated data
  AudioData &audio = seg.audio();
  ...
} else {  // no AR data, fall back to normal mode
  ...
}
```

PS Attractor, PS Spray and PS Blobs are the three effects that need this. It is
the **only** place the distinction belongs: an effect that simply wants numbers
calls `seg.audio()` and takes whatever it gets, which is what keeps the other 34
audio effects animating in the simulator.

Two fields are yours to write, not the source's. `audio.max_vol` and
`audio.bin_num` are inputs to the beat detector that effects such as Waterfall,
Ripple Peak and Puddlepeak set from their own sliders, exactly as WLED's
`u_data[6]` and `u_data[7]` work. The real source deliberately never overwrites
them, so whatever an effect wrote reaches the next analysis block. The
simulation does overwrite them, on every frame, because upstream's
`simulateSound()` does (`wled00/util.cpp:664-665`); an effect's own values
therefore last for the rest of that frame and no longer, on both sides.

## 8. Parallel batch workflow

Twelve batches are being ported at the same time, one porter each. `BATCHES.md` is
the assignment; this is the mechanic.

**Set up your worktree.** From the repository, with `<batch>` being your batch name
from `BATCHES.md`, for example `fx_1d_c`:

```
git worktree add C:\tmp\wfx-wt\<batch> -b <batch>
```

That gives you a private checkout on a private branch. Work there and nowhere else.

**Edit exactly one file**, `components/wled_fx/wf_effects_<group>.cpp`, where
`<group>` is the group id from `BATCHES.md` (`fx_1d_c` owns `wf_effects_1d_c.cpp`).
A private `wf_effects_<group>.h` beside it is allowed if your file genuinely needs
one, and nothing else is. Nothing outside your file names your group: both the
ESPHome codegen and the simulator's CMake discover groups by scanning the effect
sources for the two lines in section 2, so there is no shared list to update and
therefore nothing for two porters to collide on.

**Check `wf_fx_shared.h` before you write a helper.** The helpers the first wave
of batches turned out to share were absorbed into the engine after that wave
merged, and they are declared there: `get_random_wheel_index()`,
`tristate_square8()`, `sin_gap()`, `speed_formula_l()`, `blink()`,
`mode_gravcenter_base()`, `mode_colorwaves_pride_base()`, `fx_prng()`, `IBN`, the
`SPOT_TYPE_*` spotlight shapes and `SPOT_TYPES_COUNT`, and the `Ripple`, `Spark`,
`Flasher` and `Gravity` structs. `ULTRAWHITE` and
`DARKSLATEGRAY` are in `wf_color.h`, `M_PI` and `M_TWOPI` in `wf_math.h`, and
`FRAMETIME_FIXED`, `NUM_COLORS` and `FAIR_DATA_PER_SEG` in `wf_segment.h`.

**A helper the engine still does not have goes in your own file**, as a function
inside the anonymous namespace, even when another batch needs the same one. Two
files carrying their own copy of `chase()` is expected and correct; the anonymous
namespace is what stops them colliding at link time. Never edit an engine file to
add a helper, and **list every helper you had to add in your final report**, so the
engine can absorb the ones that turn out to be shared.

**Build and run only your group:**

```
cmake -S tools/sim -B tools/sim/build -G Ninja
cmake --build tools/sim/build
tools/sim/build/wled_fx_sim --group <group> --out tools/sim/out
```

Re-run the `cmake -S` line after you add your first effect: the group table is
generated at configure time, not build time. Before that, `--group <group>` prints
"no effects matched" and exits 1, which is correct for an empty batch.

**Check the firmware actually contains your effects.** A group that fails to link
is silent, and this is the only cheap way to catch it:

```
esphome compile examples/strip-esp32.yaml
strings examples/.esphome/build/wled-fx-strip/build/firmware.factory.bin | grep "Your Effect"
```

There is no `firmware.bin`; the image to grep is `firmware.factory.bin` under the
build directory named after the config. Compile `examples/strip-esp32-arduino.yaml`
too, because the Arduino framework trips over things esp-idf does not (see the
macro pitfall in section 6).

Grep for the display name of every effect you added. A missing one means the group
object or the guard is spelled wrong, not that the effect is broken.

**Commit on your branch. Do not push, do not merge, do not rebase onto another
batch.** The branches are merged centrally, and because every batch is one new
file plus nothing else, those merges cannot conflict. Leave the worktree in place
when you are done.

## 9. Checklist

- [ ] Body copied from `refs/WLED/wled00/FX.cpp` at v16.0.1, transform applied, nothing else changed
- [ ] Upstream author credit comment kept
- [ ] Metadata string copied verbatim into `ENTRIES`
- [ ] Effect wrapped in `#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_<NAME>`, and the same macro listed in the group guard
- [ ] `ENTRIES` switched from the empty `nullptr` form to the array form, `ENTRY_COUNT` follows it
- [ ] Group object declared `extern` before its definition
- [ ] No per-frame allocation, no VLA, no `millis()`, no `delay()`
- [ ] Nothing outside your own translation unit was touched
- [ ] `wled_fx_sim --group <yours>` green at all three geometries
- [ ] 1D effects also run clean under `--map 4` at 64x64
- [ ] Contact sheet PNGs actually look like the effect
- [ ] `esphome compile examples/strip-esp32.yaml` and `examples/strip-esp32-arduino.yaml` green, and `strings` finds every effect name in the image
- [ ] Every helper you had to write yourself is listed in your final report

## 10. Particle effects

The 31 `PS *` effects run on a port of WLED 16.0.1's `FXparticleSystem`, which
lives in `components/wled_fx/wf_particle.h` and `wf_particle.cpp`. Upstream's
struct, field and method names are kept exactly (`ParticleSystem2D`, `sprayEmit`,
`setWallHardness`, `PartSys->particles[i].ttl`, ...), so a particle effect body is
copied the same way any other effect body is. Only the four points below differ
from `FX.cpp`.

**Include the particle header.** Particle translation units are the one exception
to "include `wf_effects.h` and nothing else":

```cpp
#include "wf_effects.h"
#include "wf_particle.h"
```

**The init functions take the segment first.** Upstream reaches the segment
through the global `SEGMENT`; here it is passed in once and the system keeps it.
That is the only change to the init line, and `updateSystem()` and everything else
stay exactly as upstream writes them:

| WLED 16.0.1 | wled_fx |
|---|---|
| `initParticleSystem2D(PartSys, sources, extra, adv, sizectl)` | `initParticleSystem2D(seg, PartSys, sources, extra, adv, sizectl)` |
| `initParticleSystem1D(PartSys, sources, fraction, extra, adv)` | `initParticleSystem1D(seg, PartSys, sources, fraction, extra, adv)` |
| `SEGENV.data` (recovering the pointer) | `seg.data` |
| `PS_P_RADIUS`, `PS_P_RADIUS_1D`, `PS_P_MAXSPEED`, … | unchanged, `constexpr` rather than `#define` |

**The init pattern**, unchanged from upstream apart from the above. Every particle
effect starts like this, and the shape matters: the system is allocated on the
first call only and recovered from `seg.data` on every later call.

```cpp
void mode_particlexyz(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))
      FX_FALLBACK_STATIC;  // allocation failed or not 2D
    // one-time source setup goes here
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // always first: refreshes dimensions and data pointers
  // per-frame settings calls, then emitting, then
  PartSys->update();  // moves, collides and renders
}
```

`#define NUMBEROFSOURCES n` becomes a `constexpr uint32_t NUMBEROFSOURCES = n;`
**inside the effect function**, not at file scope: several effects in the same
translation unit use the same name with different values, exactly as upstream does
with `#undef`.

**Settings are per-frame, not per-init.** Upstream calls `setWrapX`,
`setGravity`, `setMotionBlur`, `setUsedParticles` and friends on every frame after
`updateSystem()`, because they read the sliders. Keep them where upstream has
them. The only calls that belong inside the `seg.call == 0` branch are the ones
upstream puts there.

### Pitfalls

* **`updateSystem()` must be the first thing you call** on the system each frame.
  It re-points every internal pointer at `seg.data` and re-reads the canvas size.
  Touching `PartSys->particles` before it is undefined behaviour after a resize.
* **The PS object lives in `seg.data`.** It is placement-new'd into the segment
  data blob, so it is destroyed the moment the effect changes (`Segment::reset()`
  frees the blob). Never cache the pointer across frames in a file static.
* **`initParticleSystem2D()` fails on a 1D canvas**, on purpose, and
  `initParticleSystem1D()` fails on a single pixel. Both deallocate the segment
  data before returning false, which is why the `PartSys == nullptr` check after
  the if/else is not redundant. Both paths must end in `FX_FALLBACK_STATIC`.
* **Extra scratch goes through `additionalbytes`.** Ask for it in the init call and
  read it back from `PartSys->PSdataEnd`, as PS Fire does for its frame timer. Do
  not call `seg.allocate_data()` yourself in a particle effect: that is what the
  particle system's own allocation already did, and a second call frees it.
* **Rendering writes the canvas directly.** The 2D system and the unmapped 1D
  system use `seg.canvas()->pixels()` as their framebuffer and blend additively
  into it, so a particle effect must not also call `seg.fill()` or
  `seg.fade_to_black_by()`: use `setMotionBlur()` and `setSmearBlur()`, which is
  what upstream does.
* **The particle system's y axis points up.** `(0,0)` is bottom left in particle
  coordinates and top left in the canvas; the render functions flip it. Effect code
  works in particle coordinates and never sees the flip.
* **Coordinates are subpixels, not pixels.** `PS_P_RADIUS` (64) subpixels per pixel
  in 2D and `PS_P_RADIUS_1D` (32) in 1D. `PartSys->maxX` is the subpixel bound,
  `PartSys->maxXpixel` the pixel bound. Mixing them up is the easiest way to get a
  particle stuck against a wall.
* **Do not size anything from `MAXPARTICLES_2D`.** Read `PartSys->usedParticles`,
  which is what `setUsedParticles()` and the allocation retry actually settled on.
* **`gammaCorrectCol` is on**, as it is upstream, so the renderer's matched pair
  runs: the per-particle brightness is gamma corrected before it is spread over the
  sub-pixels and each sub-pixel weight then gets the inverse, which makes the
  spatial falloff linear once the output stage applies gamma. Both halves are
  inside the frame. Dropping them is not the same as turning gamma off, and doing
  so is what made every particle effect dimmer and harder edged than WLED's until
  v0.3.1. Note that the pair only exists on the interpolated paths: a system at
  `setParticleSize(0)` renders one pixel per particle and gets the forward gamma
  alone, which is also what upstream does.
* **1D on a matrix.** With a 1D-to-2D mapping (`m12` other than 0) the 1D system
  renders into a local buffer carved out of `seg.data` and transfers it through
  `seg.set_pixel_color()`, so the mapping is applied for free. Check your 1D
  particle effect at 64x64 under `--map 1` through `--map 4`.
* **RAM.** A 2D system is about 24 KB on any matrix of 2048 pixels or more, because
  the particle count saturates at `MAXPARTICLES_2D`. It comes out of the same
  `platform_alloc()` (PSRAM preferred) as everything else, and `initParticleSystem2D()`
  halves the particle count and retries when the allocation fails, down to 5
  particles, before giving up and leaving you on the static fallback.

---

## 11. Performance: what the frame budget is actually spent on

Everything here was measured on an Apollo M-1, an ESP32-S3 at 240 MHz with 8 MB
of octal PSRAM driving a 64x64 HUB75 panel, against a WLED 16.0.1 device on the
same panel. The frame gate targets WLED's own 23 ms.

At v0.4.1 the port held 40 fps or better on 58 of its 223 effects, median
34.6 fps. WLED on the same panel holds it on 199 of 216, median 43.0. That gap
is across the board rather than a few heavy effects, so it was structural, and
four things turned out to be behind it.

### The frame gate can only fire on a main loop tick

ESPHome runs its component phase at most every `loop_interval_`, 16 ms by
default (`esphome/core/application.h`). A 23 ms deadline against a 16 ms tick is
meant to alternate one tick and two for an average of 23 ms, and on a cheap
effect it does: Solid measured 43.1 fps.

It stops working as soon as a rendered tick runs long. ESPHome times the next
tick from the start of the last one, so a tick that took 19 ms is followed by
the next one 16 ms after it started, three of those milliseconds already spent,
and the tick after that is a further 16 ms away. A 23 ms deadline falls in that
gap and the frame arrives at 35 ms instead of 23. An effect whose own work is
13 ms therefore renders at 29 ms, which is the 34.6 fps median almost exactly.

`loop_interval:` on the display front end asks for a shorter tick. `auto`, the
default, is a third of the frame period floored at 4 ms, and it only ever
lowers the interval, never raises it, so nothing else in the firmware loses
responsiveness it already had. The frame gate still accumulates its deadline, so
a shorter tick cannot make anything render faster than it was asked to; it only
stops it rendering slower. `loop_interval: never` leaves ESPHome alone.

This is the one change in v0.5.0 that touches the whole device rather than this
component, and it is the first one to turn off if anything else in a firmware
starts behaving oddly.

### -Os against WLED's -O2

ESPHome compiles a firmware with -Os on both frameworks. WLED's `platformio.ini`
has no `-O` flag either, so most of WLED is also -Os, but its two hottest
functions are not: `Segment::setPixelColor` and `Segment::getPixelColor` carry
`WLED_O2_ATTR`, which is `__attribute__((optimize("O2")))` (`wled00/const.h`),
and both 2D accessors are `IRAM_ATTR`.

The same idea, applied to whole translation units, is `wf_optimize.h`: a single
`#pragma GCC optimize("O2")` behind `WLED_FX_OPTIMIZE_SPEED`, included first by
every wled_fx source file. There is no other way for an external component to
change the flags of its own files, because ESPHome copies every component into
one `src/` tree and compiles it with one set of flags, so `build_flags` and
`build_src_flags` would change the whole firmware.

Every source file rather than only the hot ones, deliberately: the engine's
inline helpers live in headers, each source file emits its own copy and the
linker keeps one of them. Half at -Os and half at -O2 would make which copy
survives depend on link order.

`optimize: speed` is the default. `optimize: size` turns it off; README.md has
the flash cost per configuration.

### Asking the canvas its own size once a pixel

This was the largest single cost and the least visible.

`Segment::set_pixel_color(n, c)` called `is_active()`, `length()`, `is_2d()`,
`width()` and `height()`, then `set_pixel_color_xy()`, which called
`is_active()`, `width()` and `height()` again before the raw setter called
`width()` once more. Every one of those reached through `canvas_` as a null test
and two loads, and both setters were out of line in `wf_segment.cpp` where
nothing could hoist them. That is a dozen redundant loads behind two calls, for
one store, 4096 times a frame.

WLED does not do this. `Segment::_vWidth`, `_vHeight` and `_vLength` are static
members refreshed once per segment per frame in `beginDraw()` (`wled00/FX.h`),
so `SEGLEN` is a single load and not a recomputation. The port now caches the
same figures on the Segment, filled by `set_canvas()`, which is the only moment
they can change: the canvas is allocated once at setup and never resized under a
running effect. `length()` keeps its live switch on `map1d2d`, because that is a
public field an effect may read, and only the geometry underneath it is cached.
`set_pixel_color_xy()` and `get_pixel_color_xy()` moved into the header, where
at three member loads, a compare and a store they are smaller than the call that
used to reach them.

Host benchmark over all 223 effects at 64x64, v0.4.1 against v0.5.0, both at
-Os: 94.0 us a frame down to 68.5. With `optimize: speed` as well, 54.8 us, a
total of 1.71 times. Fire 2012, which WLED runs at the cap and this port needed
16 ms of render for, is 2.51 times faster.

### The canvas in PSRAM

ESPHome's default `RAMAllocator` prefers PSRAM, so the 16 KB canvas, the 12 KB
frame buffer and everything else went to external RAM. Every effect reads and
writes the canvas several times a frame in scattered order, and none of that is
the long sequential burst PSRAM is good at.

WLED puts its own segment buffer in PSRAM too, and its comment there says the
cost is under 2 percent. That comment is about a buffer written once per pixel
per frame by the effect, not one read back five times per pixel by a blur, so it
does not settle the question here. `canvas_memory:` makes it answerable on
hardware rather than in an argument. `auto`, the default, takes internal RAM
when the allocation leaves 48 KB of internal heap and a 16 KB largest block
behind, and falls back to PSRAM when it does not. `dump_config` prints where
each buffer landed.

### What the output path costs

The frame push is a flat 6.8 ms on the M-1, the same for all 223 effects, which
is 30 percent of the budget before any effect code runs. Reading the whole path
turned up three things.

**The flip does not wait.** esp-hub75's `GdmaDma::flip_buffer()` splices one
descriptor `next` pointer and swaps two indices. There is no semaphore, no
vsync and no EOF callback anywhere in the driver, and the panel refreshes itself
from a circular GDMA chain at about 76 Hz whether or not anything pushes a
frame. So the 6.8 ms is work, and the `render_max` spikes in the profile are not
a missed refresh.

Worth knowing while it is being said: "double buffered" here means the
descriptor chain is never torn, not that the CPU stops writing a buffer the
panel is reading. The swap is immediate on the CPU side but the DMA only follows
the spliced pointer when it finishes the current chain, up to 13 ms later.

**Most of that work is not ours, and WLED pays it too.** The driver stores a
frame as eight bit planes of pre-serialised 16 bit HUB75 control words, so one
pixel is eight read-modify-writes on words 128 bytes apart: 32768 of them for a
64x64 frame. There is no batch path for arbitrary content and there could not
easily be one, because each destination word interleaves the two panel halves
plus fixed address and latch bits. WLED's HUB75 bus does the same thing through
`drawPixelRGB888` at the same 8 bit depth on an S3, and on top of it keeps three
full-frame copies per frame where this port keeps one.

**What was ours.** The frame buffer was PSRAM-first like everything else, so the
conversion loop wrote 12 KB to external RAM and the driver read it straight back
from there; it now asks for internal RAM. And the loop had two versions with a
per-channel multiply and shift in the dimmed one; the master brightness is now
folded into the gamma table, so there is one loop and three lookups whatever the
brightness is, for exactly the same bytes.

Not done: skipping the push when the frame is unchanged needs a 16 KB comparison
to find out, which costs about what it saves, and almost no effect produces two
identical frames anyway.

### Rendering on the second core

Out of scope for v0.5.0, and worth writing down now that the output path is
understood well enough to size it.

The shape would be: core 1 renders frame N into one canvas while core 0 converts
and pushes frame N-1 from another, with two canvases and a handoff between them.
The upper bound on what that buys is the smaller of the two halves, so with
render at 6 ms and output at 5 ms it removes about 5 ms from a frame, and on the
effects already at the cap it removes nothing; it is worth the most exactly
where render and output are closest, which after v0.5.0 is the middle of the
distribution rather than the slow tail. Pacifica at 47 ms of render would still
be Pacifica.

What it would take: a second canvas, so 32 KB rather than 16 KB, which is more
than the internal RAM budget above allows and would put one of them back in
PSRAM and hand some of the gain straight back. A task pinned to core 1 with a
two-slot handoff and no allocation on either side of it. An answer for the
effect scratch block, a single buffer the running effect owns that the renderer
would then be touching from another core. And an answer for the audio analysis,
which already runs as its own task and which the audio effects read without a
lock, on the assumption that the reader is the main loop.

The honest order is: re-profile on hardware after v0.5.0, see how much of the
distribution is still short of the cap and by how much, and only then decide
whether 16 KB of RAM and a cross-core handoff are worth those milliseconds.

### Measuring it yourself

`tools/sim/bench.cpp` is the host benchmark and the golden-frame check in one
binary, because they want the same run.

    powershell -File tools\wsl\wfx.ps1 bench --repeats 3
    powershell -File tools\wsl\wfx.ps1 optbench
    powershell -File tools\wsl\wfx.ps1 golden

`bench` prints microseconds a frame for every effect, slowest first. `optbench`
builds the engine twice, at -Os and at -O2, and runs both, which is the
measurement behind the `optimize:` option. `golden` compares a hash of every
pixel of every frame of all 223 effects against `tools/sim/golden.txt`, and the
sweep runs it too. An optimisation that moves a pixel fails there, which is what
makes "faster" safe to claim; a commit that deliberately corrects an effect
regenerates the file with `bench --write tools/sim/golden.txt` and says so.

On the device, the "Profile run" button in the hardware test harness walks every
effect and logs one line each. docs/HARDWARE-TESTING.md has the commands.

## Deviations from PLAN.md

Recorded here as PLAN.md asks. Everything else in the P1 scope was built as
specified.

1. **The effect registry is not self-registering.** PLAN.md says "each exporting
   one registration table" via per-translation-unit registration. A constructor
   chaining into a linked list was built first and proved not to work: ESPHome
   compiles the generated sources into a static library, so an effect translation
   unit that nothing references is never extracted from the archive and its static
   initialiser never runs. The build succeeded and the effects were silently absent
   from the firmware. The table of groups is now emitted by ESPHome codegen into
   the generated `main.cpp` (and by CMake into `groups_generated.cpp` for the
   simulator), built by scanning the effect sources for the two-line convention in
   section 2. The property PLAN.md actually wanted is preserved: porters still add
   one file and change nothing else.

2. **`Segment` does not carry the WLED fields that have no meaning here.**
   `start`, `stop`, `startY`, `stopY`, `offset`, `grouping`, `spacing`, `opacity`,
   `cct`, `blendMode`, `mode`, `name` and the segment option bits are gone. There
   is one canvas and one segment; ESPHome's `partition` component already covers
   splitting a strip, and the light layer already owns brightness. `reverse`,
   `mirror`, `transpose` and friends are not implemented either; the display front
   end's pixel mapping and the light front end's `serpentine` cover the real cases.

3. ~~`M12_S_PINWHEEL` is not implemented.~~ **Closed before the parallel batches
   started.** All five mapping modes now work. Upstream sizes its two Bresenham
   coordinate arrays as stack VLAs from the matrix dimensions; here they are one
   allocation made alongside the canvas in `Segment::set_canvas()`, sized
   `2 rays x (max(width, height) + 2) points x 2 coordinates`, and upstream's
   file-static `prevRays` became a segment member. Exercise it with
   `wled_fx_sim --map 4`.

4. **Scrolling Text is reduced.** WLED 16.0.1 renders live date and time tokens
   (`#DATE`, `#TIME`, …) through `localTime`, decodes UTF-8, and loads variable
   width custom fonts off the filesystem through `FontManager`. This port takes its
   text from a `text:` option or the `wled_fx.set_text` action, handles ASCII 32 to
   126, and ships the five fixed-width console fonts WLED uses. The upstream time
   tokens are better served in ESPHome by a lambda against a `time:` component, and
   custom font loading needs a filesystem story this component does not have yet.

5. **`FX_FALLBACK_STATIC` is still a macro.** It expands to a fill and a `return`,
   so it cannot be a function, and rewriting the 40-odd upstream uses by hand would
   hurt diffability for no gain. ESPHome's rule is about `#define` for constants,
   which this is not.

6. **WLED type names are kept.** `CRGB`, `CHSV`, `CRGBW`, `CHSV32`,
   `CRGBPalette16`, `RGBW32()`, `R()`, `G()`, `B()`, `W()`. They are all inside
   `esphome::wled_fx`, so nothing leaks into the global namespace, and keeping them
   is what lets 200 more effect bodies be copied rather than rewritten.

7. **`examples/host.yaml` does not compile on Windows.** ESPHome's `host` platform
   needs `sys/ioctl.h` and `sys/select.h`, which MinGW does not provide, so the
   build fails before it reaches any wled_fx source. The config is valid and the CI
   workflow builds it on Linux. Nothing about it is component specific.

8. **The display front end is a `PollingComponent`, not a plain `Component` with a
   gated `loop()`.** ESPHome's `register_component()` emits `set_update_interval()`
   whenever the config carries `update_interval`, so the natural spelling of the
   option is the polling schema. Behaviour is the same: one render, one
   `draw_pixels_at()` and one `display->update()` per interval.

9. **`examples/strip-esp32-arduino.yaml` was added.** PLAN.md asks for the strip
   example to build under both frameworks, which needs two files.

10. **The audio contract is a struct, not `um_data_t`.** PLAN.md already calls for
    this ("audio effects read a plain struct, not `um_data` void pointers"), and
    `wf_audio.h` defines it now, ahead of the microphone and the FFT, so the audio
    batches are not blocked. Field order matches upstream's `u_data` indices, the
    four MoonModules extras are included because they cost 14 bytes, and
    `simulateSound()` is ported so the effects run with nothing attached. The
    `AudioSource` interface a real microphone will implement is declared but has no
    implementation yet.

11. **`Segment` gained `now_us`.** Seven upstream effects call `micros()` for
    sub-millisecond pacing. Rather than let effects read a clock, the frame
    timestamp is published in microseconds as well. It is derived from `now`, so it
    steps 1000 at a time and the simulator stays reproducible.

12. **`M12_P_CORNER` read-back was wrong and is fixed.** `get_pixel_color()`
    returned `(i, 0)` for every matrix; upstream uses the longest dimension, so a
    tall matrix reads `(0, i)`. No P1 effect noticed, because none of them read
    back through a corner mapping.

13. **Arduino compatibility shims live in `wf_math.h`.** `constrain`, `radians`,
    `degrees`, `sin_t`, `cos_t`, `tan_t` and the `pgm_read_*` family, all as
    ordinary functions inside `esphome::wled_fx`. They exist so effect bodies stay
    verbatim; they do not pull in Arduino.

14. **The particle system is `wf_particle.h` / `wf_particle.cpp`, not a `particle/`
    subfolder.** PLAN.md asks for `particle/`. Both source scanners are flat: the
    simulator's CMake globs `components/wled_fx/wf_*.cpp` and ESPHome's codegen
    globs `wf_effects_*.cpp` in the same directory. A subfolder would have meant
    editing both, for no gain, so the particle system follows the `wf_` convention
    every other engine file uses.

15. **The particle system reaches the segment through a stored pointer, and its
    collision bin array is heap allocated.** Two consequences of not having WLED's
    global `SEGMENT`, and of the no-VLA rule:
    * `initParticleSystem2D()` and `initParticleSystem1D()` take `Segment &seg` as
      their first argument; the system stores it and `updateSystem()` keeps its
      upstream signature. That is the only change to a particle effect body.
    * `handleCollisions()` puts its `binIndices` array on the stack as a variable
      length array sized from `usedParticles`, up to 2 KB. Here the array is part of
      the particle system's own allocation (`calculateBinArrayEntries2D()`, half the
      particle count in 2D and a quarter in 1D, rounded to an even count), so the
      binning behaviour is identical and nothing is on the stack. It costs about
      1 byte per allocated particle, 2 KB on a 2048 particle system.

    Upstream's `Segment::maxMappingLength()` is also not on `Segment` here; it is
    `particleMaxMappingLength(seg)` in `wf_particle.h`, so no engine file changed.

16. **The particle system keeps one arithmetic path, not two.** Upstream carries a
    `#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ESP8266)` alternative in
    `applyFriction()`, `collideParticles()` and the collision friction, replacing a
    division by 255 with a shift by 8 plus a sign correction. It is a speed
    optimisation whose rounding differs slightly from the division, so keeping both
    would make the host simulator unrepresentative of a C3 build. This port keeps
    only the division, which is the branch every ESP32 except the C3 already takes.
    Two small loose ends from upstream are also tidied: `ParticleSystem1D::bounce()`
    is declared upstream and never defined, so it is dropped here (the 1D wall
    bounce is inlined in `particleMoveUpdate()`), and `ParticleSystem2D::setSaturation()`
    is declared upstream and never defined, so it is implemented here rather than
    left as a link error waiting for the first effect that calls it.

17. **The audio pipeline runs entirely on one task.** WLED splits it: the FFT
    task does the sampling, the transform and the GEQ mapping, while the main
    loop runs `getSample()`, `agcAvg()` and `limitSampleDynamics()` hundreds of
    times a second against whatever the task last published, with no
    synchronisation at all. Here `AudioProcessor::process_block()` runs the whole
    chain on the analysis task, and reproduces the loop cadence with upstream's
    own hiccup-compensation loop: the volume filters and the AGC controller are
    stepped in 2 ms increments to catch up with the block. Same numbers, without
    the data race, and the result is a pure function of the samples and the clock
    so the host test is reproducible. The hand-off to the render loop is a
    field-wise copy under a spinlock, which is also what keeps `max_vol` and
    `bin_num` effect-owned.

18. **`autoResetPeak()` uses a fixed 50 ms.** Upstream takes
    `max(50, strip.getFrameTime())`. There is no strip here, and both front ends
    default to 23 ms, so the lower bound is always the one that applies.

19. **`FFT_PREFER_EXACT_PEAKS` is not optional.** Upstream can be built with a
    flat top window instead of Blackman-Harris. This port always uses
    Blackman-Harris and its `FFT_DOWNSCALE` of 0.40, builds the window itself
    rather than calling `dsps_wind_*`, so both FFT backends see identical input,
    and has no integer FFT path: esp-dsp's float transform is used on every ESP32
    variant.

20. **`Segment::has_real_audio()` is a new accessor with no upstream
    counterpart.** It answers the question upstream asks by looking the
    audioreactive usermod up in `UsermodManager`. Three effects branch on it
    rather than reading the data, so without it their non-audio animation is
    unreachable: `seg.audio()` never fails, it falls back to `simulateSound()`.
    PS Attractor, PS Spray and PS Blobs use it and keep both upstream branches.
    See section 7.

    One difference from upstream is worth knowing. Upstream's question is answered
    at the moment the usermod is registered, so it is effectively a build-time
    constant. This one is answered by the source, which reports no data until it
    has analysed its first block, roughly a tenth of a second after boot. So with
    a microphone wired up, those three effects run their non-audio branch for the
    first few frames and then switch. The flag only ever goes from false to true,
    so it cannot oscillate; it is a cosmetic transient at start-up.

21. **PS Sonic Stream clamps a particle index upstream never clamps.**
    `seg.aux1` tracks the last emitted particle across frames and is used to
    index `PartSys->particles` before anything re-checks it. `usedParticles` is
    recomputed on every init and drops when `initParticleSystem1D()`'s allocation
    retry loop halves the particle count, so a shorter system can inherit an index
    that is now past the end of the array. One comparison resets it to 0, which
    only changes which particle is checked for spacing, and the next emit
    overwrites it anyway.

22. **PS Springy's spring force array is checked, not just trusted.** Upstream
    sizes a stack VLA from `usedParticles`; here the same bytes are asked for as
    `additionalbytes` and read back from `PartSys->PSdataEnd`. The arithmetic does
    hold: the request is for the pre-retry particle count, the retry loop only
    halves `numParticles`, and `setUsedParticles()` caps `usedParticles` at
    `numParticles`. Alignment holds too, because every block
    `updatePSpointers()` walks past is a multiple of 4 bytes long, including the
    3 byte `PSadvancedParticle1D` array, whose length is a multiple of 4
    particles. That is a long chain of invariants living in another file, and
    `updateSystem()` recomputes `PSdataEnd` from the live canvas size on every
    frame while the allocation does not move, so the effect bounds-checks the
    region against `seg.data_size()` and falls back to static rather than writing
    past it. The check cannot fire today and there is no slack at all in the
    region, so the comparison is `>` and not `>=`.

    The cost, which upstream does not pay, is about four bytes of particle system
    memory per particle. On a board short of heap that can push
    `initParticleSystem1D()`'s retry loop one halving further than upstream would
    go, and the strip then shows fewer, more widely spaced particles at the same
    Density setting.

23. **DNA Spiral's step count is `abs()`, not upstream's `abs8()`.** The effect
    draws a gradient line between two points and sizes the loop with
    `abs8(x - x1) + 1`, which narrows to `int8_t`. `x` and `x1` each run to
    `cols - 1`, so on a panel 129 or more pixels wide their difference can be
    exactly -128; `abs8(-128)` is -128 again, `unsigned steps` becomes
    4294967169 and the loop runs for four billion iterations. On a device the
    watchdog fires. WLED never sees it because its matrices are narrower than
    that, and below 129 columns `abs()` and `abs8()` agree on every input the
    effect can produce, so this is the same effect everywhere upstream runs and
    a working one everywhere else.

    The other six `abs8()` call sites in `wf_effects_2d_b.cpp` are left alone:
    each one stores its result in a `uint8_t` or feeds it into arithmetic, so
    the same narrowing costs a wrong pixel rather than a hang.

24. **The frame gate accumulates its deadline; upstream's frame clock is the
    strip's.** ESPHome's main loop ticks about every 16 ms and its scheduler
    re-arms an interval from the moment the callback ran, so asking for WLED's
    23 ms FRAMETIME gets 32 ms and every effect runs at 31 fps. The gate in
    `WledFxController` advances the deadline by one period per frame instead, so
    the period alternates between one tick and two and the average is the one
    configured; it resynchronises rather than bursting if the loop stalls. That
    is also why the display front end is a plain `Component` with a `loop()` and
    not a `PollingComponent` any more, which supersedes deviation 8.

25. **The effect scratch block grows and is reused; WLED frees it.** WLED's
    `allocateData()` frees and reallocates on every effect change. On an ESP32
    with no PSRAM and a long uptime that is a fragmentation source, because the
    blocks are tens of kilobytes and every effect wants a different size, and
    ESPHome's own contributor guidance treats allocate and free cycling after
    setup as a reliability risk. Here `Segment::reset()` marks the block stale
    rather than freeing it, and `allocate_data()` zeroes and reuses anything
    already large enough, reallocating only to grow. The trade is idle RAM,
    bounded by the largest effect the device can run at all, against never
    fragmenting the heap after setup. `data_size()` still reports what the
    running effect asked for, not the capacity, so PS Springy's bound check is
    unchanged. `deallocate_data()` is still a real free, because the particle
    system uses it to make `data` null when there is no valid system in it.

26. **A 2D-only effect on a 1D strip renders a solid colour, and the config
    validation says so.** 56 of the 223 effects are 2D only. Every one of them
    falls back to `FX_FALLBACK_STATIC` on a canvas one pixel high, either from
    its own `is_2d()` guard or because `initParticleSystem2D()` refuses, which
    is what upstream does. That is deterministic but never what anyone meant, so
    naming one of them as the light effect's `effect:` on a geometry the
    validation can see is one dimensional is a config error instead. The display
    front end takes its size from the display, which is not knowable at config
    time, so the check does not apply there.

27. **The simulated sound's beat is the port's, not upstream's.** With no
    microphone, `seg.audio()` returns a port of WLED's `simulateSound()`, and
    all four of its mode bodies are copied line for line. One line outside the
    switch is not: upstream raises the beat flag with
    `samplePeak = hw_random8() > 250;` (`wled00/util.cpp:662`), five draws out
    of 256, about 2 percent of frames, and because that is a draw per frame
    rather than per second its rate follows the frame rate: about 0.85 peaks a
    second at a 23 ms frame and 0.39 at the simulator's 50 ms.

    Puddlepeak is the effect that needs it. It draws nothing except on the
    flag, and measured a mean brightness of 0.01 against a real device's 2.46.
    Ripple Peak is dimmed rather than blanked, 0.39 against the device's 1.28,
    and Waterfall is not peak-gated at all: its `else` branch
    (`wf_effects_audio_fft.cpp:259-261`, upstream `wled00/FX.cpp:7524-7526`)
    paints a column on every `secondHand` tick whatever the flag says, and the
    flag only picks the colour of the newest column. Round 1 recorded all three
    as blank, which was wrong about two of them. Because the line sits outside
    the switch it is the same in all four modes, and all three effects pin
    `si=0` in their metadata, so no choice of simulation mode changes it.

    Here the flag is one peak per beat of 120 bpm, the tempo the simulated
    spectrum's own `beatsin8_t(120 / (i + 1), ...)` calls are already built on,
    counted off the frame timestamp rather than the frame number so the rate is
    the same at the simulator's 50 ms and either front end's 23 ms.

    The flag is the port's. The draw is upstream's and is still made:
    `hw_random8()` is called and its value thrown away, because the generator
    is shared with every effect and consuming one fewer random number per frame
    would put every audio effect on a different random sequence from
    upstream's. Round 1 dropped the draw and the repository noticed indirectly,
    through the simulator's black allowance for Fw Starburst audio, which had
    to be widened from two pixels to three when the shared sequence moved.

    Visible consequence: Puddlepeak and Ripple Peak animate with no microphone,
    at two flashes a second, where upstream's simulation leaves Puddlepeak
    dark. With a real microphone attached nothing here runs at all.

28. **`CRGBW` zeroes its white channel; upstream leaves it uninitialised.**
    `hsv2rgb_rainbow(..., raw, true)` writes three bytes, and upstream's
    `//rgbdata[3] = 0; // white` is commented out
    (`wled00/src/dependencies/fastled_slim/fastled_slim.cpp:105`), so
    `CRGBW(CHSV32(...))` at `wled00/colors.h:165` carries whatever was on the
    stack in `w`. Every such constructor and assignment here sets `w = 0`.

    This is undefined behaviour upstream and the port is not going to reproduce
    it, but it is exactly the kind of quiet improvement the copy-the-body rule
    exists to stop, so it is written down. Visible consequence, and it is a real
    one: on a device, WLED's live view maps RGBW to RGB with `qadd8(w, r)`
    (`wled00/ws.cpp:236-238`), so the stack junk lands on all three channels.
    Color Clouds on the reference device shows a uniform grey floor of about 60
    counts per channel that this port does not, worth 56 counts of mean
    brightness and 31 points of coverage in a comparison. The port is right and
    the difference is upstream's. The comparison tool calls this out rather than
    reporting it as a coverage difference.

29. **Fire 2012 clamps its ignition area to the segment length.** Upstream is
    `const uint8_t ignition = MAX(3,SEGLEN/10);` (`wled00/FX.cpp:2168`) with no
    upper bound, and then indexes the heat array with `hw_random8(ignition)`.
    On a two pixel segment that reads and writes the third element of a two
    element array. One comparison here caps it at `seg_len`. Visible
    consequence: none above three pixels, where the two are identical; at two
    pixels, a defined picture instead of whatever was next in the scratch
    block. Two pixels and not one, because both sides return early at one
    (`wled00/FX.cpp:2158`, `if (SEGLEN <= 1) FX_FALLBACK_STATIC;`). A two pixel
    light is a real configuration for this component in a way it is not for
    WLED.

30. **Multi Comet audio's `m12` default is renumbered from MoonModules' 7 to
    stock WLED's 4.** MM inserts Circle and Block into the 1D-to-2D mapping
    enum above Corner (`WLED-MM/wled00/FX.h:406-413`), so its Pinwheel is 7
    where stock WLED's is 4 (`wf_segment.h`, `Mapping1D2D`). The port uses stock
    WLED's numbering throughout, so the one MM metadata string that names
    Pinwheel is translated. It is the only `m12` in the `mm` group outside the
    0 to 3 range the two agree on. Do not change it back.

    It is not the only metadata string that differs from its upstream, which is
    what this said until round 2 checked all 223 against both sources. Of those
    223, 221 match byte for byte; this one and Scrolling Text's (deviation 31)
    differ as described, and seven MM strings have the trailing moon glyph
    stripped from the effect name: Fireworks audio, Fw Starburst audio, GEQ 3D,
    Paintbrush, Popcorn audio, Snow Fall, and Multi Comet audio, which drops it
    on top of the `m12` change. The removal is deliberate and the reason is at
    `wf_effects_mm.cpp:1178-1179`: the glyph is MoonModules' marker for its own
    effects in its own UI, it is not part of a name anyone types in YAML, and a
    multi-byte glyph in a name is one more thing between an effect and a Home
    Assistant entity id.

31. **Scrolling Text's metadata drops `rev=0,mi=0,rY=0,mY=0` and blanks one
    slider name.** Those four keys set segment reverse and mirror, which
    deviation 2 says do not exist here. Recorded separately because deviation 4
    talks only about fonts and time tokens, and a future upstream merge would
    otherwise put them back.

    The fifth change in the same string is the seventh slider name: upstream's
    `Custom Font` (`wled00/FX.cpp:6567`) is empty here
    (`wf_effects_2d_a.cpp:267`), which is how a metadata string hides a control
    that does nothing. Deviation 4 covers the font handling; this is what the
    control layer shows for it.

32. **Fw Starburst audio counts its stars in an `unsigned`, not MoonModules'
    `uint8_t`.** MM has `uint8_t numStars = 1 + (SEGLEN >> 3);`
    (`WLED-MM/wled00/FX.cpp:3568`), which wraps to 0 at a segment length of
    2040 and renders nothing at all above it. Stock WLED 16.0.1 fixed exactly
    this (`WLED/wled00/FX.cpp:3621`) and the port takes the fixed form. Visible
    consequence: identical below 2040 pixels, where both clamp to the same
    `maxStars`; from 2040 to 2047 pixels the MM original goes dark and this
    does not, and the same again at 4088 to 4095 and every 2048 pixels after
    that, because a `uint8_t` wraps rather than saturating.

33. **Both of WLED's gamma stages are here, and the output stage is the front
    end's option.** WLED uses gamma in two places. `gamma8()`, `gamma8inv()`
    and `gamma32inv()` are called inside effect and particle bodies, to
    pre-compensate a value so that the output stage lands where the author
    wanted; that kind is visible in WLED's own pre-output buffer and is
    reproduced here with WLED's real tables at its default gamma of 2.2
    (`wled00/wled.h:414`), independent of any option. `gamma32()` over the
    finished frame in `show()` (`wled00/FX_fcn.cpp:1723`) is the other, and
    here it is the display front end's `gamma_correct`, or the ESPHome light's
    own on the light path.

    Since v0.4.0 the display front end defaults that option to 2.2, WLED's
    value, because 1.0 is not a neutral choice: it is the one value that leaves
    the engine's pre-compensation uncancelled, so Matrix's spawn pixel reaches
    the panel at the 215 that `gamma8inv(175)` produced instead of the 175 the
    effect asked for.

    Three things a reader matching a device needs.

    *The gate.* Upstream's output gamma is conditional on `gammaCorrectCol`
    (`wled00/FX_fcn.cpp:1713`), which defaults to true (`wled00/wled.h:412`)
    and is a setting a user can turn off. This port pins it true, so a WLED
    device with colour gamma switched off cannot be matched by any setting
    here. `gammaCorrectBri`, the separate stage at `wled00/FX_fcn.cpp:1800`
    (`if (gammaCorrectBri) b = gamma8(b);`), defaults to false and has no
    equivalent here either.

    *A hub75 panel.* WLED's HUB75 builds define `-D NO_CIE1931` (WLED
    `platformio.ini`, the shared `[hub75]` flags), so the panel library adds no
    curve of its own and gamma 2.2 is the only one. ESPHome's hub75 driver
    applies CIE1931 unless told otherwise (esp-hub75
    `include/hub75_config.h`, `HUB75_GAMMA_MODE 1`). Matching a WLED panel
    therefore takes both halves: `gamma_correct: 2.2` on `wled_fx` and
    `gamma_correct: LINEAR` on the display. The shipped matrix configurations
    do both and config validation warns when a hub75 display is left on its own
    curve underneath this one.

    *A light.* ESPHome applies the light's own `gamma_correct`, default 2.8,
    and applies it after its brightness scaling
    (`light/esp_color_correction.h:33-36`), where WLED corrects first and lets
    the bus scale afterwards (`wled00/FX_fcn.cpp:1802`). Setting the light to
    2.2 matches WLED at full brightness. It does not match a dimmed one, and
    that is not fixable from this side: at half brightness a buffer value of
    128 leaves a WLED strip as `gamma2.2(128) * 128/255` = 28 and an ESPHome
    strip as `gamma2.8(64)` = 5. Buffer to LED at full brightness, for the
    three paths: 32 becomes 3, 3 and 1; 128 becomes 56, 56 and 37; 215 becomes
    175, 175 and 158, reading WLED, the display front end at 2.2 and an
    ESPHome light at 2.8.

    The component's own master brightness, the Colour 1 light, is applied after
    the gamma table on the display front end, which is WLED's order. On the
    light front end it is applied before `set_rgbw()` and ESPHome's curve then
    bends it, for the same reason: the light owns the stage after this one.

34. **"* Random Cycle" has no settings, and its blend window comes from a
    constant.** Upstream's `Segment::handleRandomPalette()`
    (`wled00/FX_fcn.cpp:435`) draws a new harmonic palette every
    `randomPaletteChangeTime` seconds and then blends the live palette onto it
    fast enough to arrive inside `strip.getTransition()`, after which it stops
    and holds. `RandomPalette::step()` does the same arithmetic, with the two
    numbers as constants: five seconds (`wled00/wled.h:613`) and 750 ms
    (`wled00/wled.h:609`), neither of which is a setting here. The 750 ms is
    not a transition, and the port has none; it is only the window the blend
    is sized to fit, and upstream uses the transition time for it because that
    is the number a WLED user already has. `useHarmonicRandomPalette`, which
    upstream defaults to true, is also not a setting: the harmonic generator
    is always used after the first palette.

    Until v0.4.1 this blended once per frame instead of sizing the blend to
    the window, which at a 23 ms frame takes about 5.9 seconds to walk 255
    blend steps: longer than the five second change interval, so the palette
    never arrived. On the device this reads as a much dimmer, muddier
    "* Random Cycle": two random palettes averaged partway are less saturated
    and less bright than either of them. Measured against the reference device
    at matched controls, the port rendered Fire 2012 on "* Random Cycle" at
    0.68 of the device's mean brightness and Hiphotic at 0.71; every other
    palette on those two effects was inside the noise floor.

35. **A control moved at runtime is pinned, and that includes the palette.**
    `Engine::sticky_` defaults to true, so the first palette picked from the
    select survives every later effect change. WLED reloads the effect's
    declared palette instead (`wled00/FX_fcn.cpp:616-617`,
    `if (sOpt >= 0 && loadDefaults) setPalette(sOpt);`, with `loadDefaults`
    true from its own UI). The four hardware test firmwares turn pinning off
    at boot through their "Pin controls" switch and so match WLED; the plain
    examples have no such switch and do not. There is no YAML key for it yet,
    only `Engine::set_sticky_controls()`.

36. **A hub75 panel has two brightnesses, and neither defaults to a WLED
    device's.** WLED's HUB75 bus does not scale pixels in software at all:
    `BusHub75Matrix::setPixelColor` leaves `nscale8_video` commented out and
    `setBrightness` does nothing but `display->setBrightness(_bri)`
    (`wled00/bus_manager.cpp`), so a WLED panel is gamma-corrected bytes at a
    hardware duty of `bri`. Here the same bytes leave the display front end,
    and there are two places to dim them: the Colour 1 light, which is a
    software multiply on the finished 8 bit frame and therefore loses shadow
    detail the way WLED never does, and the panel's own `brightness`, which is
    the hardware duty and is the one that matches. The esp-hub75 driver
    defaults that to 128, which is WLED's own `DEFAULT_BRIGHTNESS` of 127 and
    not what a device somebody has turned up is running at: the reference
    device sits at 220, so it emits about 1.7 times the light for the same
    frame. Dim with the panel brightness, not with Colour 1.
