# Round 1 tooling notes

What the comparison tooling got wrong, and what would measure better next time.
Ordered by how much damage each one did to round 1.

## T1 The direction and speed estimator aliases by 180 degrees, and its sign is inverted

`tools/compare/metrics.py:38-111` (and the identical copy in
`tools/reference/capture_wled.py`) phase-correlates frames `F // 8` apart, which
at about 20 fps over 6 s is roughly 0.8 s. A 32x32 circular correlation wraps at
16 pixels, so anything moving faster than about 20 px/s is reported with the
wrong sign, and the reported speed is the true displacement modulo 32 rather
than the true displacement. This is the direct cause of most of the 111 flags:
"motion is opposite" appears on 20 effects and "motion goes X on WLED and Y on
the port" on another 30, and not one of them survived re-measurement.

There is a second, independent bug in the same function. It takes the
correlation peak index as the displacement, when for `R = fa * conj(fb)` the
peak lands at minus the displacement. Fed a synthetic pattern moving one column
right per frame, the shipped estimator returns `direction: left`. Every
direction word in `REPORT.md` is therefore the opposite of the truth. It does
not affect the reference-against-port comparison, because both sides go through
the same code, but it makes the report unreadable and it hid the sign error
behind a symmetric mistake.

**Improvement.** Correlate adjacent frames (about 50 ms, unambiguous up to about
320 px/s at this resolution), take the median of the per-pair velocities rather
than the mean of the displacements, negate the peak index, and repeat the
measurement at gaps of 1, 2 and 4 frames. If the answer does not scale linearly
with the gap, the pattern is periodic and the direction should be reported as
unknown rather than as a number. A 30-line replacement was used for this review
and reproduced ground truth exactly on synthetic translations in all four
cardinal directions plus a diagonal.

## T2 The engine-against-front-end attribution is unsound, because the engine captures were taken at different settings

`tools/compare/compare.py:303-317` decides "engine" or "ESPHome front end" by
running the same comparison against the engine capture. But the engine captures
were taken with "the effect's WLED metadata defaults" (their own `meta.json`
says so), while the reference and port captures were matched to whatever the
device actually had, which for many effects is a palette carried over from the
previous effect. The engine row in the side-by-side images is visibly a
different colour from the other two rows for Hiphotic, Noise2D, Metaballs,
Julia, Waving Cell, Blobs, Colored Bursts, Drift, DNA, Tartan, Frizzles,
Lissajous and more.

The engine capture also runs on a virtual clock that steps 50 ms per render
(`tools/sim/main.cpp:77`, `ANIM_STEP_MS`), so the engine sees 20 effect frames
per second where the device and the port see about 43. Every effect whose
animation is driven by `seg.call` rather than by `seg.now` therefore runs at
half rate in the engine and at full rate in the port, which by itself can flip
the attribution. On top of that, `tools/sim/main.cpp:96-107` carries a per-effect
PACING table that gives some effects a head start (PS Galaxy 1500 ms), so those
engine rows are at a different point in the animation entirely.

**Improvement.** Give the simulator the same `--match-reference` treatment the
snapshot harness has, drive it at 23 ms per render for comparison runs, and
disable the PACING table for them. Until then, print "attribution unavailable"
rather than a wrong answer, or at minimum suppress it for any effect whose
engine palette differs from the reference's.

## T3 A 6 second window cannot see effects that are slower than it

Sweep and Wipe were flagged on coverage and brightness by 172 and 66 counts;
both turn out to have identical sweep rates and a period longer than the
capture. Tartan is driven by beats of 2 and 3 bpm, periods of 20 and 30 s.
Slow Transition's own slider is measured in minutes. PS Galaxy needs longer than
6 s to form a spiral. Halloween Eyes idles for long random intervals and both
sides were dark for the whole window.

**Improvement.** A per-effect capture length, defaulting to 6 s but keyed off
the effect's own timebase: any effect whose body calls `beatsin` with a bpm
below about 12, or whose speed slider is a duration, gets 30 s or more. Cheaper
alternative: capture 6 s twice with a gap and require a flag to reproduce in
both windows before it is reported.

## T4 The reference is not a fixed point, and single captures were treated as if they were

Game Of Life's reference capture shows pastel cyan cells at pal=11 where the
port shows a saturated rainbow, hue distance 0.85, the sixth worst flag in the
report. Two fresh captures of the same effect at the same settings, one from the
idle preset and one with the palette settled by a preceding effect, both render
the saturated rainbow. Whatever produced the original frame did not reproduce
twice.

**Improvement.** Capture the reference twice in separate passes and compare the
two reference runs with the same metrics before comparing either against the
port. Anything that does not agree with itself is a measurement, not a finding.
That second pass also gives a baseline for how much of each metric's spread is
just run-to-run noise, which is the number the report's thresholds should be
set from.

## T5 Effects that latch a colour at `call == 0` capture the previous palette

`capture_one_effect()` sends the effect id and the palette in one request, then
settles. WLED cross-fades a palette change over its transition time, so the very
first frame of the new effect, which is where `call == 0` runs, still sees the
old palette. Any effect that samples the palette once and then propagates it
keeps the old colours for the whole capture. Aurora is the clean case: it picks
its wave colours at `call == 0` and the reference came out pink at pal=50 where
the port is green. Selecting a preceding effect so the palette was already
settled made the device render green (scratchpad `devD`).

**Improvement.** Send the palette, wait out the transition, then send the effect
id in a second request; or send `{"transition": 0}` alongside. Record in
`meta.json` that it was done, so the next reader does not have to rediscover
this.

## T6 The live view adds the white channel to R, G and B, and upstream leaves that channel uninitialised

`refs/WLED/wled00/ws.cpp:236-238` maps RGBW to RGB with `qadd8(w, r)` and so on.
Upstream's `CRGBW(CHSV32)` never writes `w` (the line is commented out in
`fastled_slim.cpp`), so any effect that builds a colour that way ships whatever
was on the stack. On this device it is a constant of about 60, and Color Clouds
consequently shows a uniform grey floor in the live view that the panel itself
would not show, worth 56 counts of mean brightness and 31 points of coverage
against the port. Reproduced on the device at pinned settings.

**Improvement.** The comparison cannot see the white channel, so it cannot
subtract it. What it can do is flag it: when the reference's minimum per-channel
value across a frame is well above zero and roughly equal on all three channels,
say so in the report rather than reporting a coverage difference. A better fix
is out of scope here but worth an upstream issue against WLED.

## T7 Thresholds are absolute where the quantity is relative

`compare.py:63-66` flags a hue distance above 0.35, coverage above 0.25 and
brightness above 40 of 255. On effects that light 30 pixels out of 1024, a hue
histogram is built from a handful of samples and swings wildly: PS Fireworks
scores a hue distance of 1.00 on about 50 lit pixels, Fireworks 0.41 on 28
against 72. Eight of the report's colour flags are on effects with under 1
percent coverage.

**Improvement.** Weight or suppress the hue comparison by the number of lit
pixels it was built from, and report a confidence interval rather than a point
value. The same applies to the frozen test: a fixed `mean_frame_change < 0.5`
put Fireworks 1D and Perlin Move on opposite sides of a line they both sit on.

## T8 Small things worth fixing while in there

* `subsample()` asserts the port's dimensions divide the reference's. It will
  throw on any panel that is not an exact multiple of the live view, which is
  most of them once someone runs this on a 32x8.
* The report has no way to say "this effect has no reference". Four WLED-MM
  effects silently vanish from a report that claims to cover the port.
* Nothing records the port capture's `gamma_correct` in the comparison output,
  even though it decides whether the particle findings in F1 are visible. It is
  in each capture's `meta.json` but not in `REPORT.md`.
* Reference and port folder names use different id numbers for the same effect
  (`Akemi_186` against `Akemi_166`), so joining them by folder name fails and
  every tool has to read `meta.json` to build the map. Writing the effect name
  as the folder name would remove that.
