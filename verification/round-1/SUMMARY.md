# Round 1 summary

## Counts

Effects examined: 108. That is all 64 offered on a 2D layout, 34 further
non-audio effects from the flagged list, and a gross-failure sweep over the
audio set of which 10 are called out individually.

By verdict:

| Verdict | Count |
|---|---|
| defect | 28 rows, covering 5 findings |
| artefact | 31 |
| expected | 26 |
| clean | 23 |
| unresolved | 2 |

By severity:

| Severity | Findings |
|---|---|
| S1 | 0 |
| S2 | 2 (F1, F2) |
| S3 | 3 (F3, F4, F5) |

Shared-root-cause engine bugs: 3 of the 5 findings (F1 covers 20 effects, F2
covers 3, F5 covers 3). Single-effect bugs: 0. The other two findings are a
documentation gap (F3, four items) and a coverage gap (F4, four effects).

## Top findings, one line each

1. **F1** The 2D particle renderer runs with `gammaCorrectCol` false and
   identity gamma tables, so WLED's matched gamma and inverse-gamma pair inside
   the renderer is gone and every particle effect is 0.43 to 0.85 times WLED's
   brightness with harder edges.
2. **F2** `strip.getMaxSegments()` became the literal `1`, so
   `segs <= (1 / 2)` is false and Fireworks Starburst gets 34 stars where WLED
   gets 136.
3. **F5** The simulated `sample_peak` fires on 2 percent of frames, so
   Puddlepeak, Ripple Peak and Waterfall are effectively blank with no
   microphone, against PORTING.md's promise that every audio effect animates.
4. **F3.1** `CRGBW` zeroes the white channel where upstream leaves it
   uninitialised. The port is right, it is undocumented, and it is why the
   reference's Color Clouds carries a grey floor the port does not.
5. **F4** Snow Fall, GEQ 3D, Paintbrush and Fireworks audio have no reference
   capture at all and are unverified.
6. **F3.2** Fire 2012 clamps `ignition` to the segment length where upstream
   does not; correct, undocumented.
7. **F3.3** Multi Comet audio's `m12` default is renumbered from MM's 7 to
   stock WLED's 4; correct, undocumented, and the only metadata string out of
   223 that differs from upstream apart from Scrolling Text.
8. **T1** The shipped direction estimator aliases by 180 degrees above about
   20 px/s and reports the sign backwards; it produced roughly 50 of the 111
   flags and none of them survived.
9. **T2** The engine-against-front-end attribution is unsound: the engine
   captures use metadata defaults instead of the reference's settings and run on
   a 20 fps virtual clock against the port's 43.
10. **T4** The reference is not reproducible for at least one effect (Game Of
    Life), so single reference captures cannot be treated as ground truth.

## What round 2 should look at

* Verify F1 and F2 by the measurements in `FINDINGS.md`, and re-run the whole
  comparison afterwards: F1 moves the brightness of 20 effects, so the ranking
  will change.
* Fix T1 and T2 before re-running, otherwise round 2 inherits the same 50 false
  flags and the same wrong attribution.
* Settle the two unresolved rows: Pride 2015 (40 percent brightness gap with no
  cause found) and PS Sonic Stream (needs a capture with music playing, and it
  is the effect deviation 21 is about).
* Cover the four WLED-MM effects, by capture or by reading.
* Capture the slow effects for 30 s or more (Sweep, Wipe, Tartan, Slow
  Transition, PS Galaxy, Halloween Eyes) so the phase argument does not have to
  be made from theory.
* Check the 1D layout, which round 1 did not touch at all: every effect here was
  judged on a 64x64 canvas, and the 1D-to-2D mapping modes `m12` 1 to 4 have no
  coverage in the comparison either.
* Check the light front end. Every port capture came through the display front
  end; the light front end has its own gamma and its own frame gate, and F1's
  visibility depends on exactly that.
