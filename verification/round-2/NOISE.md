# The noise floor: WLED against WLED

Before anything in this round is called a difference between the port and the
device, here is what the device disagrees with *itself* about.

## How it was measured

The whole reference set was captured a second time from the same device, with
the same tool, the same settings and the same firmware, about four hours after
the first:

    tools\reference\capture_wled.py --outdir ...\esphome-wled-fx-ref2 \
        --colors "255,160,0;0,0,0;0,0,0"

216 effects, 6 s each, 32x32 nearest-subsampled live view, factory colours,
`fxdef: true`, transition 0. Run 1 is `esphome-wled-fx-ref`, run 2 is
`esphome-wled-fx-ref2`. Both runs restored the device and verified the restore.

Every metric below was recomputed from the frames with
`tools/compare/metrics.py`, not read out of the stored `meta.json`, because run
1 predates two of the fields.

## The numbers

Absolute difference between the two runs of the same firmware, by effect class.
"other" is non-audio, non-particle; "particle" is the `PS *` family; "audio" is
anything whose metadata declares `si=` or whose name is in the audio list.

| Metric | Class | p50 | p90 | p95 | p99 | max |
|---|---|---:|---:|---:|---:|---:|
| mean brightness, of 255 | all | 0.56 | 21.1 | 35.2 | 123.4 | 170.3 |
| | other | 0.43 | 9.9 | 23.5 | 78.2 | 132.3 |
| | particle | 0.22 | 6.4 | 7.2 | 12.5 | 14.2 |
| | audio | 15.4 | 81.0 | 106.9 | 156.4 | 170.3 |
| fraction lit | all | 0.002 | 0.058 | 0.140 | 0.446 | 0.614 |
| | other | 0.001 | 0.033 | 0.049 | 0.246 | 0.519 |
| | particle | 0.003 | 0.049 | 0.056 | 0.187 | 0.230 |
| | audio | 0.027 | 0.319 | 0.399 | 0.562 | 0.614 |
| mean frame change | all | 0.16 | 2.47 | 5.31 | 32.88 | 40.50 |
| | other | 0.13 | 1.01 | 2.46 | 5.91 | 8.08 |
| | particle | 0.20 | 1.02 | 1.37 | 2.15 | 2.38 |
| | audio | 1.42 | 27.89 | 33.84 | 38.44 | 40.50 |
| hue distance, 40+ lit px/frame | all | 0.017 | 0.476 | 0.780 | 1.000 | 1.000 |
| | other | 0.007 | 0.385 | 0.771 | 1.000 | 1.000 |
| | particle | 0.022 | 0.676 | 0.820 | 0.912 | 0.935 |
| | audio | 0.137 | 0.455 | 0.589 | 0.803 | 0.859 |
| speed ratio, both sides confident | all | 1.06 | 1.39 | 1.51 | | 2.79 |

Boolean flags, counted over the 216 effects:

| Flag | Disagrees between the two runs |
|---|---|
| `frozen` (mean frame change under 0.5) | 15 of 216, 6.9 percent |
| `all_black` | 3 of 216 (PS Sonic Boom, PS Sonic Stream, Strobe) |
| motion direction word | 22 of 216, 10.2 percent |

The fifteen effects whose `frozen` flag flips between two runs of the same
firmware: Color Clouds, Frizzles, Juggles, Noisemove, PS Attractor, PS GEQ 1D,
PS GEQ 2D, PS Spray, Perlin Move, Puddlepeak, Puddles, Ripple Peak, Rocktaves,
Strobe, TV Simulator.

## The worst offenders, which are the ones round 1 kept re-discovering

Mean brightness, device against itself:

| Effect | run 1 | run 2 | difference | ratio |
|---|---:|---:|---:|---:|
| Gravimeter | 202.7 | 32.3 | 170.3 | 0.16 |
| Sweep | 43.5 | 175.9 | 132.3 | 4.04 |
| Gravfreq | 216.3 | 88.2 | 128.1 | 0.41 |
| Wipe | 150.7 | 54.1 | 96.6 | 0.36 |
| Noisemeter | 197.2 | 101.7 | 95.5 | 0.52 |
| Pixelwave | 123.9 | 33.6 | 90.4 | 0.27 |
| Sweep Random | 151.9 | 215.1 | 63.2 | 1.42 |
| Tri Wipe | 49.5 | 89.6 | 40.1 | 1.81 |
| Pride 2015 | 125.3 | 93.3 | 32.0 | 0.74 |

Hue distance, device against itself, on effects with enough lit pixels to build
a histogram from:

| Effect | hue distance, run 1 against run 2 |
|---|---:|
| TV Simulator | 1.00 |
| Wipe Random | 1.00 |
| Sweep Random | 1.00 |
| PS Ghost Rider | 0.94 |
| Theater Rainbow | 0.91 |
| Blobs | 0.88 |
| Game Of Life | 0.87 |
| Aurora | 0.78 |
| Random Colors | 0.75 |
| PS Blobs | 0.73 |
| Plasma Ball | 0.69 |
| Slow Transition | 0.68 |

Round 1 spent effort on Game Of Life and Aurora and produced two careful
explanations for why the reference was not reproducible. It was not
reproducible because *none* of these is reproducible. A second run of the
device answers the whole class in one pass.

## Thresholds

The comparison currently flags at `BRIGHTNESS_LIMIT = 40.0`,
`COVERAGE_LIMIT = 0.25`, `HUE_DISTANCE_LIMIT = 0.35` and
`SPEED_RATIO_TOLERANCE = 0.25` (`tools/compare/compare.py:61-66`). Three of the
four sit below the p95 of the device's disagreement with itself, so they cannot
separate a port defect from a second run of WLED.

What clears the noise floor, per class. A port-against-device difference should
be scored only when it exceeds all of the relevant rows.

| Metric | other | particle | audio |
|---|---:|---:|---:|
| mean brightness, counts of 255 | 24 | 8 | not scorable |
| mean brightness, ratio outside | 0.75 to 1.33 | 0.80 to 1.25 | not scorable |
| fraction lit | 0.05 | 0.06 | 0.40 |
| mean frame change | 2.5 | 1.4 | not scorable |
| hue distance | 0.78 | 0.82 | 0.60 |
| speed ratio | 1.55 | 1.55 | not scorable |

Three further rules the numbers force:

1. **`frozen` and `all_black` are not evidence on their own.** 7 percent of
   effects flip `frozen` between two runs of the same firmware. A "one side
   animates and the other does not" line needs the two mean-frame-change
   numbers printed beside it and needs both sides pooled over at least two
   runs, or it is a coin toss.
2. **Audio effects cannot be scored on brightness, coverage or frame change at
   all.** The device's own p95 is 107 counts of brightness and 0.40 of
   coverage. That is not the port against `simulateSound()`, that is a real
   microphone in a room against itself twenty minutes later. Only black,
   frozen-for-the-whole-window and wrong-axis mean anything.
3. **The per-effect spread is the threshold, not a class constant.** PS Vortex
   agrees with itself to 0.1 counts; PS Starburst disagrees with itself by 5.4
   on a mean of 5.5. A class p95 of 8 would clear PS Vortex's real defects and
   wrongly flag PS Starburst. The tool already pools runs per side and
   suppresses differences smaller than a side's own spread
   (`compare.py`, added in `e614fff`); with two reference runs that machinery
   is finally fed on both sides, and it is what took the flagged list from 111
   to 25.

## What the noise floor settles immediately

Cross the 25 surviving non-audio flags against the table above and 23 of them
are inside the device's disagreement with itself. The full effect-by-effect
working is in `VERDICTS.md`. The two that clear it are Color Clouds, which is
deviation 28 and not a defect, and PS Box.

Evidence: `esphome-wled-fx-ref\captures\`,
`esphome-wled-fx-ref2\captures\`, and the per-effect table this file was
generated from.
