# What still needs looking at on real hardware

Everything in this repository was verified in the host simulator and with
`esphome compile`. Nothing has been flashed. The simulator proves an effect does
not crash, does not write outside the canvas and does not render an empty frame;
it cannot tell you that an effect looks right, that a timing constant feels right
on a panel in a room, or that a microphone is picking up anything useful.

This is the list of things a porter or a reviewer flagged as "worth a look on
real hardware", grouped by what you have to set up to check it. Each entry says
what to look for, so a session can be planned rather than improvised.

Nothing here is a known bug. These are the places where the simulator's evidence
runs out.

---

## 1. On any strip, no panel and no microphone

Flash `examples/strip-esp32.yaml` (or the Arduino variant) onto a plain
addressable strip. Pick the effect through the `select` entity.

**Slow effects: is the pacing right, or just slow?** These four run against
wall-clock time and the simulator only sees them through an accelerated frame
period, so their real-time behaviour is untested.

* **Sunrise.** The default speed is a sixty minute sunrise, and the first
  quarter of it renders black. The simulator gives it 12 s per simulated frame
  to cover an hour in 300 frames. On hardware, check that a shorter speed
  setting still produces a smooth ramp and not a staircase.
* **Color Clouds** and **Slow Transition.** Both drift slowly by design. Confirm
  the drift is visible at all at the default speed, and that the transition is
  smooth rather than stepping between palette entries.
* **Halloween Eyes** and **Traffic Light.** Both spend most of their cycle in a
  long dwell. Check the dwell reads as deliberate rather than as a frozen effect,
  and that the transitions in and out are not skipped.

**Short pulses: does the LED driver keep up?**

* **Strobe Mega.** The pulse is 15 ms, which is shorter than one frame at the
  33 ms default update interval. Whether it is visible at all depends on the
  driver and the update rate. Try it at `update_interval: 16ms` too.

**Effects that start black.**

* **Noise Pal.** Renders black for roughly the first five seconds while the
  palette blend builds. That is upstream behaviour, but confirm it recovers and
  does not simply stay dark on a real strip.
* **Oscillate.** With the default settings only one bar is visible. Check whether
  that is the intended look or whether the defaults want changing for a strip of
  a typical length.

---

## 2. On a matrix panel, no microphone

Flash `examples/m1-hub75.yaml`. Most of these are about how an effect maps onto a
panel of a given shape, which is exactly what a simulator screenshot is worst at
judging.

**1D effects expanded onto a matrix.** Set the mapping (`m12`) through the
component and watch the column mapping.

* **Bouncing Balls** at 64 columns. The bar mapping puts each ball on a column;
  check the balls are distinguishable and that they are not all in phase.
* **Tetrix** and **Rolling Balls** on a **non-square** panel. Both assume a
  length rather than a shape. Check the piece and ball sizes still look right
  when width and height differ.
* **Sonic Stream**, **Sonic Boom** and **Springy** at 64x64 on the default
  mapping produce thin bands. Decide whether the default mapping for these three
  should be something other than what the metadata sets.

**Panels wider than 180 pixels.** Two effects run into arithmetic upstream
holds in 8 or 16 bits. Neither misbehaves, both just get duller, and both are
upstream's code unchanged, so the question on hardware is whether the duller
version is acceptable or whether this port should diverge.

* **2D Octopus** scales its radius map by `180 / max(cols, rows)`, which is
  integer zero once the larger dimension is 181 or more. Every pixel then sits
  at radius 0 and the effect renders a flat field.
* **Game Of Life** holds its glider period in a 16 bit segment field, which
  wraps above about 16384 pixels on a panel whose dimensions are coprime.
  Spaceship detection stops firing, so a stable glider is never reset.

**2D effects at panel sizes.**

* **Polar Lights** shows banding above 32 rows. Look at it on a 64 row panel and
  decide whether it needs a different scale constant.
* **Firenoise** saturates at 64x64: the whole panel goes to the top of the
  palette. Check whether a lower intensity default fixes it.
* **Meteor Smooth** with palette 0 shows speckle. Palette 0 means "use the
  segment colours", so some of this is expected; confirm how bad it is with a
  real gradient palette selected.
* **Blobs** initial radii. The first few seconds after the effect starts, before
  the blobs have grown, may look wrong. Check the start-up transient.
* **PS Galaxy** arms. The spiral arms only form after several hundred frames, so
  the first few seconds are a bright blob. The simulator now runs it for 1500
  frames to capture the formed state. Confirm the real thing settles the same way
  and that the intermediate state is acceptable to watch.
* **PS Attractor** looks clumpy and off-centre. Worth deciding whether that is
  the effect or the port. Compare against WLED on the same panel if one is
  available.
* **Paintbrush** strokes converge rather than spreading across the panel. Check
  against WLED-MM, which is where this effect comes from.
* **PS Pinball** modes. Rolling and collide are both behind checkmarks and were
  never exercised until the simulator gained a checks-on pass. They run clean;
  nobody has watched them.

---

## 3. With a real microphone

Flash `examples/m1-hub75-audio.yaml`. **The INMP441 pins in that example are
placeholders (GPIO10, GPIO11, GPIO12) and must be changed to match the wiring
before flashing.**

Everything below has only ever run against WLED's `simulateSound()`, which is a
clean synthetic signal. A room with a real microphone is not.

* **Gain and squelch defaults.** The AGC presets and the squelch threshold are
  ported from WLED's values, which were tuned against WLED's own microphone
  front end. They have never been checked against this port's input path. Expect
  to adjust them. This is the single most likely thing on this page to need
  changing.
* **Rocktaves.** The magnitude scale is the part that depends on absolute
  microphone level rather than on relative bins, so it is the effect most likely
  to look wrong with a real mic and correct in simulation.
* **Ripple Peak** and **Puddlepeak** look sparse under simulated peaks, because
  the simulation produces few beat events. With a real signal they should be much
  busier. Confirm that, and confirm they are not then too busy.
* **DJ Light** is dim. Check whether that is the simulated signal's dynamic range
  or the effect.
* **PS Spray** and **PS Blobs** now take a different code path with a real
  microphone attached than without one, which restores upstream's behaviour. Both
  branches compile and run, but only the non-audio branch has been watched. Check
  the audio branch on hardware, and check that unplugging the microphone falls
  back cleanly.
* **PS Springy AR mode** (the `AR` checkmark) reads audio to drive the springs.
  Never exercised with a real signal.
* **Analysis task priority.** The FFT runs on its own task next to a live hub75
  render loop, which is timing sensitive and is the one part of the audio design
  a host test cannot represent at all. Watch for panel tearing, dropped frames or
  a watchdog reset with audio enabled, and be ready to move the task's priority
  or core.

---

---

## 4. Frame rate

The frame clock was wrong until the review pass: both front ends asked for
WLED's 23 ms FRAMETIME and got 32 ms, because the gate re-armed from the moment
the frame ran and ESPHome's main loop only ticks every 16 ms. It accumulates
now, so the period alternates between one tick and two and the average is 23 ms.

That has only ever been reasoned about, never watched. On hardware, check that
a 64x64 hub75 panel can actually finish a frame in 23 ms with the heavier
effects, the particle systems and anything that blurs the whole canvas, and
that the light front end is not starving the rest of the loop on a long strip.
If a panel cannot keep up, `update_interval` is the lever, and the effects that
pace themselves against `seg.now` will still look right; the ones that count
frames will run slow.

---

## 5. Build configurations that compile but have never run

* **esp-dsp under the Arduino framework.** `strip-esp32-arduino.yaml` compiles
  with the audio pipeline in it, and that is all that has been verified. The
  esp-dsp component is added through `add_idf_component`, which behaves
  differently under Arduino, so a run on hardware is the only real check.
* **`examples/host.yaml`** builds in CI on Linux and does not build on Windows
  (ESPHome's host platform needs `sys/ioctl.h`). Nothing component specific, but
  it means the host front end has never been run on this machine.

---

## 6. Memory over a long uptime

The effect scratch block grows and is reused rather than being freed on every
effect change, which is a deliberate divergence from WLED aimed at heap
fragmentation on a board with no PSRAM. Nothing has run long enough to know
whether it helps. Leave a device cycling effects for a few days, watch the free
heap and the largest free block, and compare an ESP32 with PSRAM against one
without.

---

## Reporting back

When something on this list is checked, say what hardware and what settings, and
either strike the entry or replace it with what was found. An entry that turns
out to be a real defect should become an issue rather than stay here.
