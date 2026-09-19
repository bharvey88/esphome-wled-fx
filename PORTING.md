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
| `strip.getActiveSegmentsNum()` / `getMaxSegments()` / `getSegmentsNum()` | `1` | same |
| `SEGMENT.vWidth()` / `vHeight()` / `vLength()` | `seg.width()` / `seg.height()` / `seg.length()` | same |
| `FX_FALLBACK_STATIC` | `FX_FALLBACK_STATIC` | still a macro, fills with colour 0 and returns |
| `gamma8` / `gamma8inv` / `gamma32` / `gamma32inv` | unchanged | **identity here**, see section 6 |
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

## 6. Pitfalls hit while building P1

* **A `const` object at namespace scope has internal linkage.** This bit twice:
  once on `EFFECT_GROUP_*` and once on the generated table. It is also why the
  original design, a self-registering object chained into a linked list, does not
  work at all inside ESPHome: the generated sources are built into a static
  library, and a translation unit nothing names is never pulled out of the archive,
  so its static initialiser never runs. The build looks perfectly healthy and the
  effect simply is not there. If you ever suspect this, check for your metadata
  string in the firmware image with the `strings` check in section 8.
* **Gamma is off.** `gamma8`, `gamma8inv`, `gamma32` and `gamma32inv` are identity
  functions here. Keep the calls in the body so the diff against upstream stays
  clean, but do not expect them to do anything. Colour correction belongs to the
  ESPHome light layer, or to the display front end's own `gamma_correct` option.
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
every audio effect animates and renders in the simulator. The simulated frame is
generated once per timestamp, so all the effects in a frame see one coherent set
of numbers.

A real microphone is configured with an `audio:` block on the `wled_fx`
component, documented in the README. Nothing in an effect body changes either
way: `seg.audio()` returns the live analysis when a source is attached and has
data, and the simulation otherwise.

Two fields are yours to write, not the source's. `audio.max_vol` and
`audio.bin_num` are inputs to the beat detector that effects such as Waterfall,
Ripple Peak and Puddlepeak set from their own sliders, exactly as WLED's
`u_data[6]` and `u_data[7]` work. The real source deliberately never overwrites
them, so whatever an effect wrote reaches the next analysis block.

## 8. Parallel batch workflow

Twelve batches are being ported at the same time, one agent each. `BATCHES.md` is
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
therefore nothing for two agents to collide on.

**Check `wf_fx_shared.h` before you write a helper.** The helpers the first wave
of batches turned out to share were absorbed into the engine after that wave
merged, and they are declared there: `get_random_wheel_index()`,
`tristate_square8()`, `sin_gap()`, `speed_formula_l()`, `blink()`,
`mode_gravcenter_base()`, `mode_colorwaves_pride_base()`, `fx_prng()`, `IBN` and
the `Ripple`, `Spark`, `Flasher` and `Gravity` structs. `ULTRAWHITE` and
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
batch.** The orchestrator merges the branches, and because every batch is one new
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
* **Gamma is off**, so upstream's `gammaCorrectCol` branches inside the renderer are
  dead here, exactly as `gamma8()` is an identity everywhere else in the engine.
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
    default to 33 ms, so the lower bound is always the one that applies.

19. **`FFT_PREFER_EXACT_PEAKS` is not optional.** Upstream can be built with a
    flat top window instead of Blackman-Harris. This port always uses
    Blackman-Harris and its `FFT_DOWNSCALE` of 0.40, builds the window itself
    rather than calling `dsps_wind_*`, so both FFT backends see identical input,
    and has no integer FFT path: esp-dsp's float transform is used on every ESP32
    variant.
