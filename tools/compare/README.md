# Comparing this port against a real WLED device

Three captures of the same 223 effects, in the same format, so any two of them
can be put side by side.

| Capture | Tool | What it exercises |
|---|---|---|
| **WLED** | [`tools/reference/capture_wled.py`](../reference/capture_wled.py) | a real WLED 16.0.1 device, over its live view websocket |
| **port** | [`tools/snapshot/capture_port.py`](../snapshot/capture_port.py) | the whole ESPHome display front end: YAML, codegen, frame gate, `draw_pixels_at`, the display's own `update()` |
| **engine** | [`capture_engine.py`](capture_engine.py) | `Engine::render()` and nothing else, through `wled_fx_sim --anim` |

The third one is what makes the second one diagnostic. When the port disagrees
with the device and the engine agrees with it, the fault is in the ESPHome
front end; when both disagree the same way, it is in the engine. That
distinction found the red and blue swap in `push_frame_()`.

Each capture writes one folder per effect holding `frames.npz` (uint8, frames x
height x width x 3, plus per-frame timestamps in seconds), `sheet.png` (eight
evenly spaced frames, nearest-neighbour upscaled), `anim.webp` and `meta.json`
with the applied parameters and the metrics.

## Running it

`--reference` and `--port` can each be given more than once. Do: a single
six second window of an effect that is seeded from a random number generator,
or paced against a clock that does not restart with the capture, is a sample
and not a measurement.

```
powershell -File tools\wsl\wfx.ps1 snapshot
wsl -d Ubuntu-24.04 -u root -- /root/wfx/esphome-venv/bin/python \
    /mnt/c/.../tools/compare/capture_engine.py --out /root/wfx/enginecap
wsl -d Ubuntu-24.04 -u root -- /root/wfx/esphome-venv/bin/python \
    /mnt/c/.../tools/compare/compare.py \
    --reference /mnt/c/Users/bharv/development/esphome-wled-fx-ref \
    --port /root/wfx/portcap --engine /root/wfx/enginecap --out /root/wfx/cmp
```

`compare.py` tolerates a half-filled reference folder, so it can be run while
the device capture is still going. Everything it could not read is listed at
the end of the report.

## What is compared, and what is not

**Not pixels.** Every effect is seeded from a random number generator and paced
by wall-clock time. Two runs of the same effect on the same device do not agree
pixel for pixel, so two runs on different hardware minutes apart certainly do
not. What is compared is behaviour: moving or frozen, lit or black, which way,
how fast in pixels per second, how much of the frame is lit, what colours are
in it, how symmetric it is.

The metric functions live in [`metrics.py`](metrics.py) and there is one copy
of them: the reference capture tool imports them from there rather than
carrying its own, so a difference in a number is a difference in the animation.
It used to be a hand-kept duplicate, which is how a sign error survived in both
halves for a whole round. [`test_metrics.py`](test_metrics.py) feeds the
estimator synthetic patterns whose velocity is known in advance, in both
directions along both axes and on a diagonal, at speeds from 5 to 120 px/s, and
it runs in CI. `compare.py` also recomputes the reference side's metrics from
its raw frames and shouts if they do not match the `meta.json` the reference
wrote.

Three things are normalised first, and leaving any of them out turns a property
of the capture path into a finding on all 223 effects:

* **Resolution.** WLED's live view subsamples a 64x64 matrix to 32x32 by taking
  every second pixel; it does not average. The port's 64x64 frames get the same
  treatment, rather than the reference being scaled up, because scaling up
  invents detail WLED already dropped.
* **Colour depth.** ESPHome's snapshot display is a `DisplayBuffer`, so it
  stores RGB565. The reference's 8 bit pixels go through the same quantisation.
* **Gamma and brightness.** Neither side has either. WLED's live view sends the
  effect's raw render buffer, and the harness sets `gamma_correct: 1.0` and
  leaves the master at full.

Both sides select each effect by name, which reapplies its WLED metadata
defaults, the same state WLED's `fxdef: true` produces, and both set WLED's
factory colours: amber primary, nothing in the other two slots.

## Two things that bit on the first real run

**The palette carries over on the device.** WLED's `fxdef: true` leaves the
palette alone when an effect's metadata names none, so it keeps whatever the
previous effect was using; this port puts it back to Default. Capturing a
device effect by effect therefore carries a palette from one to the next, and
on the first full run that was 112 of 214 effects rendering in a different
palette from the port for a reason that had nothing to do with the port. Pass
`--match-reference` to `capture_port.py` and it reads the device's own applied
state back out and forces the same palette and controls, which took the flagged
count from 138 to 111 and the colour findings from 87 to 19.

**The motion estimate used to be wrong in two ways at once**, and about half of
round 1's flags came out of it. It correlated frames an eighth of the capture
apart, which on a 32 px frame wraps at 16 px and cannot tell 20 px/s from
-20 px/s; and it took the correlation peak as the displacement when the peak of
`F(a) * conj(F(b))` lands at minus the displacement, so every direction word was
the opposite of the truth. The two mistakes hid each other.

What it does now: phase correlation at frame gaps of 1, 2, 4 and up, sub-pixel
refined, with three guards. A gap whose displacement has wrapped past a third
of the frame is discarded and so is every longer one. The three shortest usable
gaps have to agree on the velocity, because a real translation goes twice as
far in twice the time and an alias does not. And the correlation peak has to be
at least a quarter taller than the next peak anywhere else, which is what rules
out a checkerboard or a stripe field at the resolution limit, where there
genuinely is no single answer. When any of those fails the direction is
reported as unknown rather than as a number.

It still reports one of eight compass points, so a reading 45 degrees away can
be the same motion landing either side of a boundary; those are called out and
scored low.

## Attribution, and when it is not available

The engine capture only tells you which half of the stack a difference is in if
it was taken at the same settings as the other two. Round 1's was not: it ran
at each effect's own metadata defaults while the device was on whatever palette
the previous effect had left, on a 50 ms clock against the device's 23 ms, and
with a per-effect PACING table that gave PS Galaxy a 1500 frame head start.
`compare.py` now checks the recorded settings of the two captures before
attributing anything and prints "attribution unavailable" when they differ.
`capture_engine.py --match-reference` is what makes them match: it pins every
control the reference recorded, passes `--step-ms 23` and turns the pacing
table off.

## Thresholds that are relative, and things that are not findings

* A hue histogram built from a handful of lit pixels swings from run to run.
  Below 40 lit pixels a frame the hue distance is printed as a note and not
  scored.
* When the reference's darkest pixel is well above zero and about equal on all
  three channels, that is upstream's uninitialised white channel arriving
  through the live view's `qadd8(w, r)` map and not a brighter render. It is
  called out as a note, because it inflates both coverage and brightness. See
  PORTING.md deviation 28.
* Effects the device does not have get their own section, "no reference
  available", rather than vanishing from a report that claims to cover the
  port. Nine effects come from WLED-MM and stock WLED 16.0.1 has none of them.
* The report records the output gamma each side was captured with, and how many
  effects were captured at matching controls.

## Still known to be weak

* **A capture shorter than the effect.** Sweep, Wipe, Tartan, Slow Transition,
  PS Galaxy and Halloween Eyes all have periods longer than six seconds, so
  where in the period each capture started decides the coverage and brightness
  numbers. Capture those for 30 s, or capture twice and believe a flag only
  when it reproduces in both windows.
* **A single reference capture treated as ground truth.** Game Of Life's
  reference frame did not reproduce on two later attempts. `--reference` and
  `--port` are both repeatable now, and with more than one folder each metric
  becomes the per-effect median across runs and carries its range; a difference
  smaller than a side's own spread is no longer scored. That is a floor, not a
  fix: it needs the extra capture runs to be taken. The spread is not small.
  Three runs of this port measured Tri Wipe at 200, 242 and 244 counts of mean
  brightness and Pride 2015 at 96, 114 and 169, and two runs of the device
  measured Matrix at 3.6 and 4.4.
* **The palette latched at `call == 0`.** WLED cross-fades a palette change over
  its transition time, so an effect that samples the palette once, at its first
  frame, keeps the previous one for the whole capture. Aurora is the clean
  case. Send `{"transition": 0}` with the palette, or send the palette and the
  effect in two requests with the transition time in between.

## Audio reactive effects

Ranked separately and only loosely comparable. The device has a real microphone
listening to a real room; the port has no microphone configured and runs WLED's
own `simulateSound()`. The two are reacting to different sound, so a difference
is expected and only a frozen or black frame means much.

## Joining the two sides

WLED numbers its effects differently from this port, so the folder names do not
line up and the join is on the effect name in `meta.json`. Names present on only
one side are listed in the report rather than silently dropped; during a capture
that is still running, that list is mostly "not captured yet".
