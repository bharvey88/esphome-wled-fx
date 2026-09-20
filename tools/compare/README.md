# Comparing this port against a real WLED device

Three captures of the same 223 effects, in the same format, so any two of them
can be put side by side.

| Capture | Tool | What it exercises |
|---|---|---|
| **WLED** | `tools/reference/capture_wled.py` on the `wled-ref` branch | a real WLED 16.0.1 device, over its live view websocket |
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

The metric functions live in [`metrics.py`](metrics.py) and are the reference
capture tool's own, copied rather than reimplemented so that a difference in a
number is a difference in the animation. `compare.py` recomputes the reference
side's metrics from its raw frames and shouts if they do not match the
`meta.json` the reference wrote, which is what keeps that claim honest rather
than merely stated.

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

**The motion estimate has two known failure modes.** It reports one of eight
compass points, so a reading 45 degrees away can be the same motion landing
either side of a boundary; those are called out and scored low. And it is a
phase correlation, which on a periodic pattern cannot tell a shift of `d` from
a shift of `-(period - d)`, so a tartan, a stripe field or a spiral can be
reported as moving the opposite way and be doing nothing of the kind. Open the
side-by-side before believing an "opposite" on an effect that repeats.

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
