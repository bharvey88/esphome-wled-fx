# Round 1 fixes

What was verified, what changed, and what the evidence says now. One section per
finding, in the order `FINDINGS.md` puts them, then the tooling, then the places
where the critic was wrong.

Everything here was measured against the same reference captures round 1 used,
at `esphome-wled-fx-ref`, plus three fresh device captures taken during this
work and three fresh captures of the port. The device was left as it was found:
on, brightness 220, preset 5, effect 53, all three colours black, verified by
reading `/json/state` back at the end.

Headline, for a reader in a hurry:

| Measurement | Round 1 | Now |
|---|---:|---:|
| Median port-to-reference mean brightness, the 17 particle effects F1 lists | 0.70 | 0.97 |
| Same, the 147 non-particle effects bright enough to measure | 1.00 | 1.00 |
| Fireworks Starburst stars at 64x64 | 34 | 68 |
| Fireworks Starburst brightness against the device | 0.54 | 0.96 |
| Pride 2015 brightness against the device | 0.59 | 1.07 |
| Puddlepeak mean brightness with no microphone | 0.01 | 0.54 |
| Non-audio effects flagged by the comparison | 111 | 26 |

The "now" column is the median of three capture runs of the port. One run is
not a measurement; see T3 and T4 below.

---

## F1, the particle renderer's gamma. Critic right about the mechanism, wrong about the value, and the finding was under-scoped

**Verified.** WLED's particle renderer does carry a matched pair, and both halves
are inside the frame:

* `refs/WLED/wled00/FXparticleSystem.cpp:609` and `:1466` gamma-correct the
  per-particle brightness before it is distributed;
* `:679-681`, `:804-805` and `:1533-1535` apply the inverse to each sub-pixel
  weight, with the comment "gamma is applied again in `show()` -> the resulting
  brightness distribution is linear but gamma corrected in total".
* `gammaCorrectCol` defaults to true at `refs/WLED/wled00/wled.h:412`.

The arithmetic follows. With identity gamma a sub-pixel gets `b * w`; with the
pair it gets `gamma8inv(w * gamma8(b))`, which is `b * w^(1/2.2)`. For `w` under
1 that is always larger, so the port was always darker, by more the more a
particle straddled pixels. That matches the shape of the deficit the critic
measured.

**Where the critic is wrong.** Two things.

*The gamma value is 2.2, not 2.8.* `FINDINGS.md` says to build the tables with
`NeoGammaWLEDMethod::calcGammaTable(2.8)` and to default the display front end's
`gamma_correct` to 2.8. WLED's default is 2.2, at
`refs/WLED/wled00/wled.h:414`, `WLED_GLOBAL float gammaCorrectVal _INIT(2.2f);`,
and `cfg.cpp:521` comments the JSON default as 2.2 as well. 2.8 is FastLED's
number and ESPHome's light default, not WLED's.

This is not a quibble, and the device settles it. Matrix builds its spawn colour
as `RGBW32(gamma8inv(175), gamma8inv(255), gamma8inv(175), 0)`
(`refs/WLED/wled00/FX.cpp:5732`). At gamma 2.2 that is **(215, 255, 215)**; at
2.8 it is (223, 255, 223). A fresh capture of Matrix from the device at pinned
settings reads **(215, 255, 215)** on every lit spawn pixel. The port now reads
(213, 255, 213), which is 215 through the snapshot display's RGB565 store.

*F1 is only half the finding.* `gamma8` and `gamma8inv` are `rawGamma8` and
`rawInverseGamma8` upstream (`refs/WLED/wled00/colors.h:39-40`): plain table
reads that do not consult `gammaCorrectCol` at all. Fourteen call sites outside
the particle system use them, and every one of them was also dead here. Matrix,
Twinklefox, Twinklecat, Noise Pal, Sunrise, Popcorn, DJ Light and five FFT
effects. `gamma32inv` is the same story with the flag, and it is the whole of
Pride 2015 (see below). The critic scored Matrix "clean" on "same spawn rate,
same green trail" while its spawn colour was 175 against the device's 215 and
its trail 27/130/39 against 91/187/108. The metrics did not catch it because the
hue is identical and the mean brightness difference is under the 40 count
threshold; reading the source would have.

**Changed.** `b4d7b2a`.

* `components/wled_fx/wf_color.cpp` gains `GAMMA_T` and `GAMMA_T_INV`, WLED's
  two tables at gamma 2.2, as constants. They are the exact output of upstream's
  formula; `float` and `double` agree on all 512 entries, so there is no
  precision hazard.
* `components/wled_fx/wf_color.h` makes `gamma8`, `gamma8inv`, `gamma32` and
  `gamma32inv` read them instead of returning their argument.
* `components/wled_fx/wf_particle.cpp` sets `gammaCorrectCol = true`.

The output stage is untouched and still belongs to ESPHome, which is what the
brief asks for. PORTING.md deviation 33 records the split and says that
`gamma_correct: 2.2` is the value that matches a factory WLED, because ESPHome's
light default of 2.8 is not it. No default was changed: an ESPHome light's gamma
is the user's.

**Evidence it is fixed.** Port against reference, mean brightness, matched
controls both sides. "After" is the median of three capture runs; the last
column is the range those three covered, which is what says whether a ratio can
be read to two figures.

| Effect | reference | before | after | before | after | after spread |
|---|---:|---:|---:|---:|---:|---:|
| PS Blobs | 35.2 | 15.1 | 17.8 | 0.43 | 0.50 | 5.8 |
| PS Starburst | 8.2 | 3.8 | 4.1 | 0.47 | 0.50 | 2.8 |
| PS Ballpit | 3.1 | 1.6 | 2.5 | 0.52 | 0.81 | 0.4 |
| PS Impact | 18.9 | 10.4 | 19.0 | 0.55 | 1.00 | 6.5 |
| PS Attractor | 4.8 | 2.9 | 4.8 | 0.61 | 1.00 | 0.2 |
| PS Vortex | 79.2 | 50.1 | 78.1 | 0.63 | 0.99 | 0.1 |
| PS Spray 1D | 2.2 | 1.5 | 2.1 | 0.65 | 0.94 | 0.1 |
| PS Waterfall | 63.6 | 43.5 | 61.4 | 0.69 | 0.97 | 0.7 |
| PS Box | 19.8 | 13.9 | 17.4 | 0.70 | 0.88 | 4.7 |
| PS Springy | 56.5 | 40.2 | 56.5 | 0.71 | 1.00 | 0.1 |
| PS Fuzzy Noise | 118.0 | 88.8 | 112.2 | 0.75 | 0.95 | 1.7 |
| PS GEQ Nova | 34.8 | 26.4 | 42.0 | 0.76 | 1.21 | 2.3 |
| PS Ghost Rider | 36.4 | 29.3 | 32.5 | 0.80 | 0.89 | 10.9 |
| PS Fire | 112.0 | 93.3 | 110.2 | 0.83 | 0.98 | 1.7 |
| PS GEQ 2D | 10.1 | 8.4 | 13.1 | 0.84 | 1.30 | 0.5 |
| PS Volcano | 6.1 | 5.2 | 5.7 | 0.85 | 0.94 | 0.2 |
| PS 1D Balance | 46.0 | 41.0 | 46.0 | 0.89 | 1.00 | 0.1 |

Median over those seventeen: **0.70 before, 0.97 after**. Median over the 147
non-particle effects with a reference brightness above 5: **1.000 before, 1.000
after**, so nothing else moved.

Fireworks Starburst, which belongs to F2 rather than F1, moves from 0.54 to
0.96 on the same measurement.

The critic's own quantile test, which is the sharper one, at the reference's
32x32 view over lit pixels only:

| Effect | side | q0.5 | q0.7 | q0.8 | q0.9 | q0.95 | q0.99 |
|---|---|---:|---:|---:|---:|---:|---:|
| PS Vortex | WLED | 115 | 213 | 255 | 255 | 255 | 255 |
| PS Vortex | before | 74 | 139 | 189 | 255 | 255 | 255 |
| PS Vortex | after | 115 | 213 | 255 | 255 | 255 | 255 |
| PS Impact | WLED | 115 | 156 | 180 | 238 | 255 | 255 |
| PS Impact | before | 65 | 90 | 115 | 156 | 197 | 255 |
| PS Impact | after | 115 | 156 | 172 | 222 | 255 | 255 |
| PS Box | WLED | 113 | 164 | 189 | 222 | 246 | 255 |
| PS Box | before | 82 | 115 | 148 | 189 | 222 | 255 |
| PS Box | after | 131 | 189 | 213 | 255 | 255 | 255 |
| Hiphotic | WLED / before / after | 230 | 238 | 246 | 255 | 255 | 255 |
| Noise2D | WLED / before / after | 230 | 242 | 246 | 250 | 255 | 255 |

PS Vortex is quantile for quantile identical to the device now. The two control
effects are unchanged, which is what rules out a global brightness shift.

Two ratios still sit above 1.0. PS GEQ 2D at 1.30 and PS GEQ Nova at 1.21 are
audio driven, and the device was listening to a quiet room while the port hears
a synthetic spectrum that never goes quiet; both were marked "expected" in round
1 for that reason. Everything else is between 0.88 and 1.00 apart from the two
residuals below. A single run had put PS Impact at 1.48, which is what the
spread column is for: over three runs it is 1.00.

**Two residuals, both recorded rather than closed.**

* **PS Starburst 0.50.** Six port runs measure 3.78, 3.83, 3.95, 4.09, 4.60 and
  5.40; two device runs measure 7.47 and 8.22. It is consistently about half and it is
  not the particle count: `calculateNumberOfParticles1D` and
  `calculateNumberOfSources1D` are statement for statement upstream's, and
  `MAXPARTICLES_1D` and `MAXSOURCES_1D` match. This is the first thing round 2
  should pick up.
* **PS Blobs 0.50** is expected and was already marked so: the effect branches
  on `has_real_audio()` and the two sides take different branches.

**Regression check.** `c9b945d`, in `wled_fx_effect_test`. Both tables are
recomputed from upstream's `powf` expression and compared byte for byte, so a
transcription slip cannot hide among 512 constants. Then a single particle is
placed dead centre on a pixel and again on the corner between four, and the
ratio of the two has to be `gamma8inv(63)/255` = 0.529 rather than 63/255 =
0.247; measured 0.528. The same for the 1D renderer with two pixels and
`gamma8inv(127)`: expected 0.725, measured 0.724. Neither depends on which
colour the palette handed out, because the centred render is the yardstick.

---

## F2, the segment data budget. Critic right about the bug, wrong about the number

**Verified.** The transform is real and it is dead code:
`wf_effects_1d_e.cpp:677`, `wf_effects_1d2d.cpp:519` and `wf_effects_mm.cpp:497`
all had `if (segs <= (1 / 2))`, which is `1 <= 0`. Upstream doubles twice
(`refs/WLED/wled00/FX.cpp:3617-3618` and `:3740-3741`).

**Where the critic is wrong.** `FINDINGS.md` says WLED gets 8192 bytes and 136
stars. That is the arithmetic for an ESP32 **without** PSRAM, where
`MAX_NUM_SEGMENTS` is 32 and `FAIR_DATA_PER_SEG` is 2048. The reference device
is an ESP32-S3 with 8 MB of PSRAM, so `BOARD_HAS_PSRAM` is defined,
`MAX_NUM_SEGMENTS` is 64 (`refs/WLED/wled00/FX.h:92`) and `FAIR_DATA_PER_SEG` is
1024. The budget is 4096 and the star count is **68**, not 136.

The device says so itself. `GET /json/info` returns `"leds":{..,"maxseg":64,..}`,
and `maxseg` is literally `WS2812FX::getMaxSegments()` (`json.cpp:718`). The
same response carries `"arch":"ESP32-S3"` and `"psram":8257975`.

The critic's own blob counts are closer to 68 than to 136: WLED averaged 41
connected blobs against the port's 26, a ratio of 1.6, where 136 stars against
34 would be 4.

**Changed.** `0d54ab6`.

* `wf_segment.h` keeps `MAX_NUM_SEGMENTS` and `MAX_SEGMENT_DATA` as WLED's own
  per-platform constants rather than flattening them, derives
  `FAIR_DATA_PER_SEG` from them, and adds `strip_max_segments()` and
  `strip_active_segments_num()` so the three effect bodies read exactly as
  upstream writes them. One segment is always inside `getMaxSegments() / 4`, so
  both doublings fire.
* The two host builds take the PSRAM profile, because both stand in for the
  reference hardware and a comparison is only meaningful if both sides size
  their scratch the same way.
* `Segment::allocate_data()` enforces `MAX_SEGMENT_DATA` on exactly the builds
  upstream enforces it on, the ones without PSRAM (`FX_fcn.cpp:163-171`). That
  is the "handled sensibly" half: the no-PSRAM board gets the larger fair share,
  8192, which is upstream's behaviour and looks backwards until you remember the
  budget is a share and a board that allows twice as many segments shares it
  twice as many ways, but it is not a licence to use heap that is not there.

**Memory consequence on a plain ESP32 strip build.** None measurable. The cap
cannot fire on anything shipped: the largest allocation any effect makes is a 2D
particle system at about 24 KB, and the scratch block is retained and grown
rather than freed (deviation 25), so its high-water mark is set by that and not
by an 8 KB star pool. `examples/strip-esp32.yaml` compiles to the same RAM
figure as before to within the linker's rounding; see the compile table below.

**Evidence it is fixed.** `wled_fx_effect_test` prints
`Fireworks Starburst: 4080 bytes, 68 stars` at 64x64, where it was 2040 bytes
and 34. Fireworks 1D's spark pool went from 2040 to 4084 bytes. Against the
device, Fireworks Starburst's mean brightness went from 7.33 to 12.95 against a
reference of 13.54, that is **0.54 before and 0.96 after**, over a three run
spread of 2.1.

**Regression check.** `c9b945d`. `strip_active_segments_num() <=
strip_max_segments() / 4` has to hold, `FAIR_DATA_PER_SEG` has to be derived
rather than a literal, the host budget has to be the reference device's 4096,
and Fireworks Starburst has to allocate 68 stars at 64x64.

---

## F5, the simulated peak. Critic right about the effect, wrong about upstream, twice

**Verified.** Puddlepeak, Ripple Peak and Waterfall draw only when
`sample_peak` is set, and the port's simulation raised it on about 2 percent of
frames, so they were blank.

**Where the critic is wrong.**

*"Upstream's own `simulateSound()` never assigns `samplePeak` at all, so it stays
0 forever and these effects are completely dead there."* It does assign it.
`refs/WLED/wled00/util.cpp:662` is `samplePeak = hw_random8() > 250;`. The port
had copied that line exactly. The 2 percent is upstream's, not an undocumented
improvement on it.

*"`wf_audio.cpp:84-85` set `max_vol = 31` and `bin_num = 8` on every frame, which
clobbers what an effect wrote ... upstream's `simulateSound()` keeps them as
untouched statics."* Upstream reassigns both on every call, at
`util.cpp:664-665`. They are statics, and they are overwritten every time the
function runs. The port matches upstream. What was wrong was one sentence in
PORTING.md section 7, which is about the real source and reads as though it
covered the simulation too; that sentence is fixed.

**A conflict with the brief, surfaced rather than papered over.** The brief says
that if WLED's own simulation is what makes these sparse, then "match WLED but
make the default simulation mode the one that gives peak-driven effects
something to show". No simulation mode does. `samplePeak` is assigned *outside*
the `switch` in `simulateSound()`, so it is identical in all four modes, and all
three affected effects pin `si=0` in their own metadata anyway. Choosing a mode
cannot fix it; the line has to change. That is what was done, and because the
line is mode-independent it is not a change to any of WLED's four simulations,
all of whose bodies remain copied line for line.

**Changed.** `8894a7a`. One peak per beat of 120 bpm, the tempo the simulated
spectrum's own `beatsin8_t(120 / (i + 1), ...)` calls are built on, counted off
the frame timestamp rather than the frame number so the rate is the same at the
simulator's 50 ms and either front end's 23 ms. Recorded as PORTING.md deviation
27, with the visible consequence, and the hardware checklist's line about these
effects "looking sparse" is corrected.

**Evidence it is fixed.** Mean brightness in the simulator over six seconds at
64x64: Puddlepeak 0.01 to **0.54**, Ripple Peak 0.39 to **1.44**, Waterfall's
frame change from 6.01 to a mean brightness of **86.4**. The device, hearing a
real room, measures Puddlepeak at 2.46 and Ripple Peak at 1.28. Puddlepeak is
now the same order as the device rather than three orders below it, which is
what the critic asked for, but it is not equal to it and two flashes a second is
a choice and not a measurement. It is documented as such.

**Regression check.** `c9b945d`. Every one of the four simulation modes has to
produce between one and four peaks a second; the rate has to be the same at a
50 ms frame as at a 23 ms one; and all three peak-driven effects have to render
something.

---

## F3, the undocumented deviations. Critic right on all four

All four are real, all four are the port being right and upstream being wrong or
undefined, and none was recorded. They are PORTING.md deviations 28 to 31 now,
plus three more the WLED-MM review turned up. `d7037cf`.

1. **`CRGBW` zeroes the white channel** where upstream leaves it uninitialised
   (`fastled_slim.cpp:100-104`, the line is commented out). The port is right
   and it is not going to reproduce undefined behaviour; deviation 28 records
   it, including why the reference device's Color Clouds carries a grey floor of
   about 60 counts that the port does not: the live view maps RGBW to RGB with
   `qadd8(w, r)` (`ws.cpp:236-238`), so the stack junk lands on all three
   channels. The comparison tool now says so in the report instead of reporting
   a coverage difference.
2. **Fire 2012 clamps `ignition` to the segment length.** Upstream indexes a
   two element heat array with `hw_random8(3)`. Deviation 29.
3. **Multi Comet audio's `m12` renumbered from MoonModules' 7 to stock WLED's
   4.** Deviation 30, with the enum on both sides so nobody changes it back.
4. **Scrolling Text's metadata drops `rev`, `mi`, `rY`, `mY`.** Deviation 31.

Three more, from reading all nine WLED-MM effects against MM's own source:

5. **Fw Starburst audio counts stars in an `unsigned`** where MM uses a
   `uint8_t` that wraps to 0 above 2040 pixels and renders nothing. Stock WLED
   16.0.1 fixed the same line. Deviation 32.
6. **The simulated beat**, deviation 27 above.
7. **The two gammas**, deviation 33, which is what PORTING.md sections 3, 6, 7
   and 10 used to get wrong.

---

## F4, the effects with no reference

**Verified and extended.** The critic named four. There are nine, and they are
exactly the `mm` group: Meteor Smooth, Party jerk, Popcorn audio, Multi Comet
audio, Fw Starburst audio, Fireworks audio, GEQ 3D, Paintbrush and Snow Fall.
The list was derived by differencing all 237 registered names against all 217
metadata names in stock WLED's `FX.cpp`, not by hand. Meteor Smooth is in stock
WLED's source but its `addEffect` is commented out at
`refs/WLED/wled00/FX.cpp:11058`, so it is MM-only at runtime and correctly
classified.

**Marked, not counted as findings.** `63965cb`. The comparison report has a "no
reference available" section now, which is neither a finding nor a pass, and the
report explains that nine of them exist only in WLED-MM.

**Reviewed by reading**, since that is the only ground truth available. All nine
bodies were diffed against `refs/WLED-MM/wled00/FX.cpp` allowing for the
documented transforms, and then cross-checked by comparing the full ordered
sequence of numeric literals and of comparison and arithmetic operators in each
function pair. Every difference resolved to a documented transform. Metadata
matches MM byte for byte apart from the trailing moon glyph and the one
deliberate `m12` renumber, which is correctly applied: MM inserts its two extra
mapping modes above Corner, so 0 to 3 stay put and only 7 becomes 4. The other
three `m12` values in the batch are in the 0 to 3 band and are untouched.

One real divergence, now deviation 32 (Fw Starburst audio's `numStars` width).
Two things that are not bugs today and are worth a round 2 look:

* **GEQ 3D and Paintbrush shorten a depth line before the bounds check** rather
  than after, as MM does inside one `drawLine`. Every endpoint either side
  reaches is provably in range (`pPos <= cols-1`, `constrain`ed projector, a
  `horizon` bounded by `map2`'s output range, `beatsin8_t(..., 0, cols-1)`), so
  nothing out of range is ever passed.
* **Snow Fall sizes its bit grid from `seg.length()`**, which is the *virtual*
  length here and the *physical* length upstream. They agree only while `m12` is
  `M12_PIXELS`, which it is: `m12` comes from effect metadata alone, Snow Fall's
  string sets none, and there is no setter, YAML key or entity for it. If `m12`
  ever becomes user settable, this one needs a guard.

---

## Pride 2015: the same root cause as F1

**Cause found.** `mode_colorwaves_pride_base` is statement for statement
upstream's, with the same integer types, the same `seg.now` handling and the
same brightness-depth arithmetic. The one line that separates Pride 2015 from
Colorwaves is `refs/WLED/wled00/FX.cpp:1985`:

    newcolor.color32 = gamma32inv(newcolor.color32);

`gamma32inv` is `NeoGammaWLEDMethod::inverseGamma32`, which applies the inverse
table when `gammaCorrectCol` is set, and it was an identity here. Colorwaves
takes the other branch and has no such call, which is exactly why the critic saw
a 40 percent gap on one and not on the other. Nothing else in the body, in
`beatsin88_t`, in `blend_pixel_color` or in `color_from_palette` differs.

**Evidence.** Matched settings both sides, `pal=11, sx=128, ix=128, c1=128,
c2=128, c3=16`, factory colours. Mean brightness:

| Run | Value |
|---|---:|
| device, round 1 capture | 124.8 |
| device, fresh capture during this work | 103.2 |
| port before | 73.6 |
| port after, three full runs | 96, 134, 167 |

Before, the port sat below every device reading. After, its median of 134
against the round 1 reference's 124.8 is a ratio of 1.07, and the device's range
[103, 125] sits inside the port's [96, 167]. The brightest pixels moved from
(189, 16, 24) to (197, 36, 49) against the device's (215, 58, 54). The residual
spread is the effect's own: its brightness is driven by
`beatsin88_t(341, 96, 224)` and three other beats whose periods are longer than
a six second window, so where the capture starts decides the number. That is the
same phase problem as T3 and it is why this is reported as three runs.

Fixed in `b4d7b2a` along with the rest of the gamma work; no separate change was
needed, which is itself the evidence that the cause was correctly identified.

---

## Tooling

### T1, the direction estimator. Critic right on both counts

**Verified by construction.** For `R = F(a) * conj(F(b))` the correlation peak
lands at minus the displacement from `a` to `b`. Fed a single bright column
stepping one pixel right per frame, the shipped estimator answered "left". And a
32x32 circular correlation wraps at 16 pixels, so a baseline of `F // 8` frames,
about 0.8 s, cannot tell +20 px/s from -20 px/s.

**Changed.** `59700b7`. Phase correlation at frame gaps of 1, 2, 4, 8, 16 and
32, sub-pixel refined with a parabolic fit, with three guards:

* a gap whose median displacement has wrapped past a third of the frame is
  discarded, and so is every longer gap;
* the three shortest usable gaps have to agree on the velocity, because a real
  translation goes twice as far in twice the time and an alias does not;
* the correlation peak has to be a quarter taller than the next peak anywhere
  else in the frame, which is what rules out a pattern at the resolution limit
  where there genuinely is no single answer.

When any of those fails the direction is "none/unclear" rather than a number.

**Validated** by `tools/compare/test_metrics.py`, which is new and runs in the
sweep and in CI: broadband textures translated in the Fourier domain at exactly
known velocities, both directions on both axes and two diagonals, at 5, 20, 60
and 120 px/s, plus a still frame, an all black frame, white noise and a one
pixel checkerboard. Measured: 8 px/s right reads as `right` at (7.66, -0.02);
120 px/s reads 118.3; one column per 50 ms frame reads `right` at 20.01 px/s;
the checkerboard and the noise both come back unknown at zero confidence.

The reference capture tool imports these functions now instead of carrying a
hand-kept duplicate. The duplicate is how the sign error came to exist in two
places at once.

### T2, the attribution. Critic right

`compare.py` compares the recorded settings of the two captures before
attributing anything and prints "attribution unavailable", naming the controls,
when they differ. `capture_engine.py --match-reference` makes them match: it
pins every control the reference recorded, passes the simulator's new
`--step-ms 23` and turns the per-effect PACING table and its warm-up off.
`63965cb`.

### T3 and T4, the capture window and the fixed point. Critic right, and it is worse than stated

**Measured.** Three runs of this port, identical build and settings, on the same
effect:

| Effect | port, median of three | range over the three | device |
|---|---:|---:|---:|
| Pride 2015 | 134 | 71 | 103, 125 |
| Colorwaves | 90 | 48 | 115 |
| Tri Wipe | 242 | 44 | 49 |
| PS Ghost Rider | 32.5 | 10.9 | 36 |
| PS Impact | 19.0 | 6.5 | 19 |
| Matrix | 3.5 | 1.9 | 3.6, 4.4 |
| PS Vortex | 78.1 | 0.1 | 80.0 |

Sweep and Wipe are worse still, at ranges of 171 and 82 counts out of 255.

PS Vortex is what a reproducible effect looks like. The others are what round 1
ranked. Ten effects moved into or out of the flagged list between two runs of an
identical build for no reason but this.

**Changed.** `e614fff`. `--reference` and `--port` are repeatable; with more
than one folder each metric becomes the per-effect median across runs and
carries its range, and a brightness or coverage difference smaller than either
side's own spread is no longer scored. The report prints how many runs each side
had and the five widest spreads. This is a floor, not a fix: it only helps if the
extra runs are taken, and the remaining phase problem for effects slower than the
window is written down in `tools/compare/README.md` under "still known to be
weak".

### T5, the palette latched at `call == 0`. Critic right

`capture_wled.py` sends `{"transition": 0}` with every state request now, so an
effect that samples the palette at its first frame samples the one it was asked
for. The transition time is restored with the rest of the device state.
`63965cb`.

### T6, the white channel floor. Critic right

The comparison flags it: when the reference's darkest pixel is at least 12
counts and about equal on all three channels while the port's is near zero, the
report says so and says it inflates coverage and brightness, rather than
reporting a coverage difference. `63965cb`, and PORTING.md deviation 28.

### T7, absolute thresholds on relative quantities. Critic right

Below 40 lit pixels a frame the hue distance is printed as a note and is not
scored. `63965cb`. The frozen test's fixed 0.5 is not changed; it now sits
behind the run-spread floor instead, which is the more general answer.

### T8, the small things

* `subsample()` no longer raises on a panel that is not a whole multiple of the
  live view. Tested on 31x17.
* The report has a "no reference available" section.
* The report records the output gamma each side was captured with, and how many
  effects were captured at matching controls.
* Folder names are left alone. Renaming them would invalidate every existing
  capture for a saving of one `meta.json` read, and the join is already on the
  name.

### Counts

| Comparison | Non-audio effects flagged |
|---|---:|
| round 1's report, round 1's tool, round 1's port capture | 111 |
| round 1's port capture, this tool | 23 |
| this port, one capture run, this tool | 29 |
| this port, three capture runs pooled, this tool | 26 |

The tooling alone takes 88 of round 1's 111 flags off the list. The single-run
count of 29 against 23 is not a regression: ten effects moved into or out of the
list between two runs of an identical build, and pooling three runs brings it to
26. The reference side is still one run, so the noise floor is only half built.
Capturing the device twice is the other half, and it is a round 2 job.

What survives, worst first: Lightning and Perlin Move on the frozen test, Wipe
Random, Tri Wipe and Color Clouds, then eleven colour flags on effects whose
colours are random draws (Sweep Random, TV Simulator, Random Colors, Chase
Random, Blobs, Game Of Life, Aurora, Theater Rainbow, Blink Rainbow, Slow
Transition, Plasma Ball), four "one side moves and the other is still" on sparse
effects, and four speed ratios. Color Clouds is deviation 28 and the report now
says so. None of them is a new flag caused by this release.

---

## Where the critic was wrong, collected

1. **Gamma 2.8.** WLED's default is 2.2 (`wled.h:414`), and the device's Matrix
   spawn colour of (215, 255, 215) is `gamma8inv(175)` at 2.2 and nothing else.
2. **F1 under-scoped.** `gamma8` and `gamma8inv` are raw table reads upstream and
   fourteen non-particle call sites were dead too, including the whole of the
   unresolved Pride 2015 row.
3. **Matrix scored "clean"** while its spawn colour was 175 against the device's
   215 and its trail (27, 130, 39) against (91, 187, 108).
4. **Starburst 136 stars.** The reference device reports `maxseg: 64` from its
   own `/json/info`, so upstream there gets 4096 bytes and 68 stars.
5. **"Upstream's `simulateSound()` never assigns `samplePeak`."** It does, at
   `util.cpp:662`, with the same expression the port had copied.
6. **"Upstream keeps `maxVol` and `binNum` as untouched statics."** It reassigns
   both on every call, at `util.cpp:664-665`. The port matched upstream; one
   sentence of PORTING.md did not.
7. **F4 named four effects.** There are nine.

Everything else in `FINDINGS.md`, `VERDICTS.md` and `TOOLING.md` held up.

---

## Builds and sizes

All eight configurations validate and compile on Windows against the ESPHome in
`C:\Users\bharv\esphome-venv`. The four hardware test firmwares were rebuilt in
`C:\tmp\wfx-flash` from the repository copies with `external_components`
repointed at the working tree, as before, and `.esphome` left alone.

The baseline is v0.3.0 compiled on the same machine with the same toolchain in a
worktree, not the figures recorded in PLAN.md, which were taken against an older
ESPHome and are 50 KB away for reasons that have nothing to do with this
release.

| Config | Flash v0.3.0 | Flash v0.3.1 | Flash delta | RAM v0.3.0 | RAM v0.3.1 | RAM delta |
|---|---:|---:|---:|---:|---:|---:|
| `strip-esp32.yaml` | 887,339 | 888,079 | +740 | 46,820 | 46,836 | +16 |
| `m1-hub75.yaml` | 882,223 | 882,827 | +604 | 110,019 | 110,019 | 0 |
| `m1-hub75-audio.yaml` | 936,311 | 936,915 | +604 | 118,931 | 118,931 | 0 |
| `strip-esp32-arduino.yaml` | 974,919 | 975,459 | +540 | 47,900 | 47,900 | 0 |

The four hardware test firmwares, which carry the tour harness and the
diagnostics on top of the engine, compile at:

| Config | Flash | RAM |
|---|---:|---:|
| `m1-test.yaml` | 1,035,679 | 113,567 |
| `m1-test-audio.yaml` | 1,095,703 | 123,855 |
| `matrix-test.yaml` | 1,023,531 | 50,220 |
| `strip-test.yaml` | 977,215 | 50,220 |

The 540 to 740 bytes are the two 256 byte gamma tables and the code around them.
RAM is unchanged everywhere except 16 bytes on the plain strip build, which is
the F2 memory claim measured rather than argued: the bigger scratch budget costs
nothing, because the scratch block's high-water mark is set by the particle
system at about 24 KB and not by an 8 KB star pool.

The sweep is green under ASan and UBSan: 223 effects at six geometries in both
control passes, the four smallest geometries, all five 1D-to-2D mapping modes,
the audio and behaviour tests, the large geometries without the sanitizers, and
the two Python checks. One entry had to be added to the simulator's
`BLACK_ALLOWED` table on the way, and the reason is in `becb81e`: Fw Starburst
audio has exactly one star below four pixels and a birth rate of up to one in
144 a frame, so whether it lights up inside a 300 frame run at three pixels is a
coin toss, and it had been on the winning side of that toss until the simulated
sound stopped drawing a random number every frame. It lights up at three pixels
with 900 frames and at four pixels with 300.

---

## What round 2 should attack first

1. **PS Starburst.** Consistently 0.5 of the device over four port runs and two
   device runs, and it is not the particle count. The gamma work moved it from
   0.47 to 0.56 and no further.
2. **The 1D layout and the `m12` mapping modes**, which round 1 did not touch
   and this round did not either. Snow Fall's grid sizing is latent there.
3. **The light front end.** Every port capture on both rounds came through the
   display front end.
4. **The effects slower than the capture window**, with 30 second captures
   rather than the phase argument from theory: Sweep, Wipe, Tri Wipe, Tartan,
   Slow Transition, PS Galaxy, Halloween Eyes.
5. **PS Sonic Stream** still cannot be settled. It needs music playing in the
   room, and the device sat in a quiet one for both rounds. The port renders
   34.5 against the device's 0.08, which is consistent with silence and with
   deviation 21 and proves neither.
