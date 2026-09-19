# Porting a WLED effect to wled_fx

This is the working document for anyone porting effects. With this page plus the
engine headers you should not need to read anything else, and you should not need
to change any engine file.

Source of truth for the effects: WLED **v16.0.1**, `wled00/FX.cpp`
(commit `29b389df1c1aaec6ff53aea742d17063b985906c`). Keep the bodies as close to
upstream as you can, so a future WLED release can be diffed against them.

---

## 1. The rules that matter most

1. **One translation unit per porter.** You create `components/wled_fx/wf_effects_<group>.cpp`
   and nobody else touches it. You never edit an engine file, an existing effect
   file, `__init__.py`, or the simulator.
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

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_B;
const EffectGroup EFFECT_GROUP_1D_B{"1d_b", ENTRIES, sizeof(ENTRIES) / sizeof(ENTRIES[0])};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_B
```

Conventions the build depends on, so get them exactly right:

| Thing | Rule |
|---|---|
| File name | `wf_effects_<group>.cpp` in `components/wled_fx/` |
| Group guard | one `#define WLED_FX_GROUP_<ID>` listing every `WLED_FX_FX_*` the file provides |
| Group object | `EFFECT_GROUP_<ID>`, same `<ID>`, with a preceding `extern` declaration |
| Per-effect macro | `WLED_FX_FX_` + the display name uppercased with every run of non-alphanumeric characters turned into one `_` |

The effect macro is what a user's `effects:` allow-list turns into, so "Fire 2012"
becomes `WLED_FX_FX_FIRE_2012` and "Colorwaves Pride" becomes
`WLED_FX_FX_COLORWAVES_PRIDE`.

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
| `millis()` / `micros()` | `seg.now` | never read the clock yourself |
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
| `MIN(a,b)` / `MAX(a,b)` | a ternary, or `std::min` / `std::max` | the macros are gone |
| `bitRead/bitSet/bitClear(v, n)` | `v & (1 << n)` / `v \|= (1 << n)` / `v &= ~(1 << n)` | Arduino macros, gone |
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

Every effect is run at 16x16, 64x64 and 60x1. The run fails if the canvas guard
bands were overwritten, if every frame came out black, or if the PNG could not be
written. **Look at the PNGs.** "Not black" is a very low bar and it will not catch
an effect that is subtly wrong.

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
  string in the firmware image (`strings firmware.bin | grep "Your Effect"`).
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
* **Effect functions must be in the anonymous namespace.** Two files porting the
  same shared base function (`chase`, `ripple_base`, `twinklefox_base` …) would
  otherwise collide at link time. Duplicating a shared base into two files is fine
  and expected; do not try to share one between files.

## 7. Checklist

- [ ] Body copied from `refs/WLED/wled00/FX.cpp` at v16.0.1, transform applied, nothing else changed
- [ ] Upstream author credit comment kept
- [ ] Metadata string copied verbatim into `ENTRIES`
- [ ] Effect wrapped in `#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_<NAME>`, and the same macro listed in the group guard
- [ ] Group object declared `extern` before its definition
- [ ] No per-frame allocation, no VLA, no `millis()`, no `delay()`
- [ ] `wled_fx_sim --group <yours>` green at all three geometries
- [ ] Contact sheet PNGs actually look like the effect
- [ ] `esphome compile examples/strip-esp32.yaml` green

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

3. **1D-to-2D mapping mode `M12_S_PINWHEEL` is not implemented** and falls back to
   `M12_PIXELS`. Upstream's implementation needs two stack VLAs sized from the
   matrix dimensions, which the no-VLA rule forbids, and no P1 effect defaults to
   it. `M12_PIXELS`, `M12_P_BAR`, `M12_P_ARC` and `M12_P_CORNER` all work.

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
