# Hardware testing session guide

Nothing in this repository has ever run on hardware. This is the plan for the
session that changes that: four firmwares, a tour mode that walks every effect
so you are not typing names into a select box 223 times, and a table to fill in
as you go.

[HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md) is the list of open questions.
This document is how to answer them in one sitting.

Everything here is reachable from a browser on the LAN. Home Assistant is
optional and nothing in the session needs it.

## Before you start

The configs live in [examples/hardware-test](../examples/hardware-test).

1. Copy `secrets.yaml.example` to `secrets.yaml` **in that folder** and fill it
   in. ESPHome looks for secrets beside the config being built, so the one in
   `examples/` does not cover these.
2. Add `examples/hardware-test/secrets.yaml` to `.gitignore`. Only
   `examples/secrets.yaml` is ignored today, so without that line your wifi
   password shows up in `git status` waiting to be committed by accident.
3. Activate your ESPHome virtual environment. On Windows, do it from PowerShell
   and not from Git Bash: the ESP-IDF toolchain installer refuses to run under
   MSys.
4. On Windows, if a build dies on a path length, copy the `hardware-test`
   folder somewhere short such as `C:\tmp\wfx-hw`, change `external_components`
   to an absolute path to the repository's `components` directory, and build
   there instead.

## The four firmwares

| Config | Hardware | What it is for |
|---|---|---|
| `m1-test.yaml` | Apollo M-1, 64x64 HUB75 | The main event. Section 2 of the checklist, plus every measurement below. |
| `m1-test-audio.yaml` | M-1 plus an INMP441 | Section 3. Adds the microphone, the analysis and the level instrumentation. |
| `strip-test.yaml` | Any WS2812 strip on a plain ESP32 | Section 1, the pacing and driver questions. |
| `matrix-test.yaml` | 16x16 serpentine WS2812 | The light front end on a 2D canvas, which is a different code path from the panel. |

Flash them in that order. `m1-test.yaml` first, because if the panel and the
frame clock are wrong then nothing else you measure means anything.

`m1-test.yaml` and `m1-test-audio.yaml` default to the rev6 panel. For a rev4,
there is a commented one line swap on the `board:` key in the `display:` block.

`m1-test-audio.yaml` has the three INMP441 pins as substitutions at the top of
the file and they are placeholders. Set them to your wiring before you build.
The HUB75 connector already takes most of the low numbered GPIOs, so check your
three against the pin list in ESPHome's
`esphome/components/hub75/boards/apollo.py`.

## Commands

These are yours to run. Serial and flashing are not something this repository
automates.

```
cd examples/hardware-test

# First flash, over USB.
esphome run m1-test.yaml

# Every flash after that, over the air, and it never touches a serial port.
esphome run m1-test.yaml --device wled-fx-m1-test.local

# Logs on their own, which is how you will spend most of the session.
esphome logs m1-test.yaml --device wled-fx-m1-test.local
```

The other three are the same with their own file name and device name:
`wled-fx-m1-audio-test`, `wled-fx-strip-test`, `wled-fx-matrix-test`.

The web interface is at `http://wled-fx-m1-test.local/` and it carries every
entity listed below. Version 3 of the web server pulls its JavaScript from the
internet the first time a browser opens it, so load it once on a machine that
has a connection.

## How the tour works

Turn on **Tour auto advance** and the device steps to the next effect every
**Tour dwell** seconds, forever, wrapping at the end. Turning the switch off
pauses on whatever is showing. Every change logs one line:

```
[I][tour:049]: [tour] 37/223 "PS Vortex" group=particle_2d
```

The two numbers are the position in the current filter and the size of that
filter, so with **Tour group** set to `All` they count to 223.

| Entity | What it does |
|---|---|
| Tour auto advance | On steps forward on a timer, off pauses. |
| Tour dwell | Seconds per effect, 1 to 120. Eight is a sensible first pass. |
| Tour group | `All`, `1D`, `2D`, `Particle 2D`, `Particle 1D`, `Audio`, `MM`, `Checklist`. |
| Next effect / Previous effect | Step by hand. Both respect the group filter. |
| Restart effect | Back to frame zero without changing effect. This is the one to use on the startup transient questions, PS Galaxy and Blobs. |
| Unpin controls | Puts Speed, Intensity, Custom and Check back on the effect's own defaults. |
| Effect / Palette | The component's own selects, all 223 and all 72. Depending on the component version these do not follow the tour, so treat **Effect name** as the authority for what is on screen. |
| Speed, Intensity, Custom 1 to 3 | The component's numbers. |
| Check 1 to 3 | The component's switches. |
| Panel brightness | hub75 brightness, panel builds only. On the strip builds this is the light entity's own slider. |
| Effect name / Effect group | Text sensors mirroring the current effect. |

`Checklist` is the group worth knowing about: it filters the tour down to the 28
effects [HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md) actually asks about.
One pass of that with a long dwell is the fastest route through the table below.

Two behaviours that will otherwise confuse you:

* Moving Speed, Intensity, Custom or Check **pins** that value, and it then
  applies to every effect the tour visits afterwards. That is the engine's
  override model, not a bug. Press **Unpin controls** when you are done poking.
* None of the control entities survive a reboot. A reflash puts everything back
  to what the YAML says.

## What to watch in the logs

* `[tour]` lines. This is your index into everything else. When something looks
  wrong on the panel, the preceding `[tour]` line names it.
* `[wled_fx]` at boot. `dump_config` prints the canvas size, the frame interval,
  how many effects were compiled in and the starting effect and palette. If the
  canvas is not 64x64 then the display size is not reaching the engine.
* `Effect frame rate`. Published every two seconds. It counts frames the engine
  really rendered, not the interval it was asked for. At the default 23 ms
  interval the ceiling is about 43. ESPHome's main loop ticks about every 16 ms,
  so a light effect sitting near 31 on everything, heavy and light alike, is the
  frame clock being re-armed by the scheduler rather than the effect being slow.
  Write the number down either way; that distinction is exactly what a real
  measurement settles.
* `Loop time`. If this climbs while frame rate falls, something is blocking the
  main loop rather than the effect being slow.
* `Heap free` and `Heap largest free block`, every ten seconds. Free heap
  drifting down over a tour is the signal to look for; the largest free block
  falling faster than free heap is fragmentation.
* `Effect data bytes`. The scratch block the running effect asked for. For the
  31 particle effects this is the particle system allocation.
* On the audio build only, once per boot and a few seconds after real sound
  first arrives:
  `[D][wled_fx.audio]: Analysis costs 1234 us per 512 sample block, 5.3% of one core`.
  It is logged once. To see it again, restart the device.

## Numbers to write down

### Frame rate at 64x64

Pause the tour on each of these, let it settle for ten seconds, then read
`Effect frame rate`. These are the heaviest things in the port: the particle
systems, the per pixel noise fields and the cellular ones.

| Effect | Group | fps | Loop time |
|---|---|---|---|
| PS Galaxy | particle_2d | | |
| PS Ballpit | particle_2d | | |
| PS Attractor | particle_2d | | |
| PS Fire | particle_2d | | |
| PS Fireworks | particle_2d | | |
| Game Of Life | 2d_b | | |
| Soap | 2d_b | | |
| Julia | 2d_b | | |
| Metaballs | 2d_a | | |
| Sun Radiation | 2d_b | | |
| Octopus | 2d_b | | |
| Paintbrush | mm | | |
| GEQ 3D | mm | | |

Anything below about 25 fps is worth a note. Anything below 15 is worth an
issue.

### Heap across a full tour

Set **Tour dwell** to 5 seconds and **Tour group** to `All`, which is about 19
minutes per pass. Read `Heap free` and `Heap largest free block` at each point.

| Point | Heap free | Largest free block | Free PSRAM |
|---|---|---|---|
| After boot, before the tour | | | |
| After one full tour | | | |
| After two full tours | | | |
| After three full tours | | | |

One tour catches a leak big enough to matter. Three catch the slow ones and show
whether the largest free block keeps shrinking while free heap holds steady,
which is fragmentation rather than a leak. If free heap is flat across all three
readings, the allocation story is fine and you can stop.

### Audio levels

On `m1-test-audio.yaml`, with the room at a normal listening volume:

| Reading | Quiet room | Music at normal volume | Music loud |
|---|---|---|---|
| Volume smoothed | | | |
| Major peak (Hz) | | | |
| Sound pressure | | | |
| AGC sensitivity | | | |
| GEQ bands (the text sensor) | | | |

What you are looking for: a quiet room reads near zero, normal volume puts the
smoothed volume somewhere in the middle of 0 to 255 rather than pinned at either
end, and the GEQ row has several bands moving rather than one band doing
everything. If everything stays at zero, lower `squelch` before touching `gain`:
the gate zeroes the filter state and not just its output, so a signal has to be
roughly five times `squelch` before anything gets through at all.

Both of those keys are compile time, so tuning them means editing
`m1-test-audio.yaml` and reflashing.

## The checklist

One row per item in [HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md). Fill in
the result column with pass, fail, or a short note. The detail on what each
question means is in the checklist itself; this is the tracking sheet.

### Section 1, any strip (`strip-test.yaml`)

| # | Effect | Question | Result |
|---|---|---|---|
| 1.1 | Sunrise | Shorter speed gives a smooth ramp, not a staircase | |
| 1.2 | Color Clouds | Drift visible at all at the default speed | |
| 1.3 | Slow Transition | Transition is smooth, not stepping between palette entries | |
| 1.4 | Halloween Eyes | The long dwell reads as deliberate, transitions not skipped | |
| 1.5 | Traffic Light | Same question, same effect family | |
| 1.6 | Strobe Mega | The 15 ms pulse is visible at 23 ms, and at 16 ms | |
| 1.7 | Noise Pal | Recovers after about five seconds of black rather than staying dark | |
| 1.8 | Oscillate | One visible bar at the defaults: intended or wrong defaults | |

### Section 2, matrix panel (`m1-test.yaml`)

| # | Effect | Question | Result |
|---|---|---|---|
| 2.1 | Bouncing Balls | At 64 columns the balls are distinguishable and not in phase | |
| 2.2 | Tetrix | Piece size looks right on a non-square panel | |
| 2.3 | Rolling Balls | Ball size looks right on a non-square panel | |
| 2.4 | PS Sonic Stream | Default mapping at 64x64 gives more than a thin band | |
| 2.5 | PS Sonic Boom | Same | |
| 2.6 | PS Springy | Same | |
| 2.7 | Polar Lights | Banding above 32 rows: does it need a different scale constant | |
| 2.8 | Firenoise | Saturates at 64x64: does a lower intensity default fix it | |
| 2.9 | Meteor Smooth | Speckle with palette 0, and with a real gradient palette | |
| 2.10 | Blobs | The start-up transient before the blobs have grown | |
| 2.11 | PS Galaxy | Arms form after a few hundred frames; the intermediate state is watchable | |
| 2.12 | PS Attractor | Clumpy and off centre: the effect or the port | |
| 2.13 | Paintbrush | Strokes spread across the panel rather than converging | |
| 2.14 | PS Pinball | Rolling and collide modes, both behind check marks, both watchable | |

Tetrix and Rolling Balls want a non-square panel. If you only have the 64x64,
set `panel_height: 32` in `m1-test.yaml` and reflash rather than skipping them,
and say in the result which geometry you used.

PS Pinball's modes are Check 1 to Check 3. Turn them on one at a time and press
**Restart effect** after each.

### Section 3, real microphone (`m1-test-audio.yaml`)

| # | Effect or question | What to decide | Result |
|---|---|---|---|
| 3.1 | Gain and squelch defaults | Whether WLED's numbers suit this input path. The most likely thing on the page to need changing. | |
| 3.2 | Rocktaves | Depends on absolute level, so this is the one most likely to be wrong with a real mic | |
| 3.3 | Ripple Peak | Busier with a real signal than with the simulation, and not too busy | |
| 3.4 | Puddlepeak | Same | |
| 3.5 | DJ Light | Dim: the simulated dynamic range or the effect | |
| 3.6 | PS Spray | The real microphone branch, which has never been watched | |
| 3.7 | PS Blobs | Same, plus a clean fall back when the microphone is unplugged | |
| 3.8 | PS Springy, AR check mark | Reads audio to drive the springs. Never exercised with a real signal. | |
| 3.9 | Analysis task next to the render loop | Panel tearing, dropped frames or a watchdog reset with audio enabled | |
| 3.10 | Analysis cost | The one line the audio source logs. Write the number down. | |

For 3.7, unplug the microphone with the device running rather than reflashing.
The source stops having data, the engine falls back to the simulation, and what
you are checking is that the fall back is clean rather than a stall.

For 3.9, watch `Effect frame rate` and `Loop time` with audio enabled against
the same effect on `m1-test.yaml`. A drop of a few frames is expected; tearing,
a stall or a reboot is not.

### Section 4, build configurations

These two are not covered by the hardware-test configs. They use the existing
examples and are only worth doing if the hardware is free.

| # | Item | Result |
|---|---|---|
| 4.1 | `examples/strip-esp32-arduino.yaml`, esp-dsp under the Arduino framework | |
| 4.2 | `examples/host.yaml`, which does not build on Windows | |

### Light front end on a 2D canvas (`matrix-test.yaml`)

Not in the checklist, but it is a separate code path from the panel and nobody
has watched it either.

| # | Item | Result |
|---|---|---|
| 5.1 | Scrolling Text reads left to right with no row mirroring | |
| 5.2 | Matrix falls downwards, not sideways | |
| 5.3 | A 1D effect expanded onto the 16x16 canvas looks like it does on the strip | |
| 5.4 | Frame rate at 16x16 on the heaviest particle effects | |

## Reporting a broken effect

One issue per effect. Four things, and the first three are not optional:

1. **Effect name**, exactly as the `[tour]` line spells it, plus the group.
2. **Geometry and config**: which of the four firmwares, panel or strip size,
   and the update interval. An effect that is wrong at 64x64 and right at 16x16
   is a different bug from one that is always wrong.
3. **The log lines**: the `[tour]` line for the effect and anything logged while
   it was running. If the device rebooted, the backtrace and the lines before
   it, not just the reset reason.
4. **A photo or a short video**, unless the failure is a crash or a black panel.
   Half of these questions are about how something looks, and prose does not
   settle them. Turn the room light off; the panel is much brighter than it
   looks to the eye and a phone will otherwise expose for the room.

Also say which controls were moved. An effect that looks wrong with a pinned
Speed from three effects ago is not a broken effect, and **Unpin controls**
before you photograph anything rules that out.

If an effect crashes the device, note whether it happens again after **Restart
effect** and whether it happens at a smaller panel size. A crash that only
appears at 64x64 points at an allocation; one that appears at any size points at
the effect body.

## When the session is over

Update [HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md): strike each entry that
is settled, or replace it with what you found and on what hardware. An entry
that turns out to be a real defect becomes an issue and comes off the page. The
README's "Nothing here has run on real hardware" line stops being true after
this session, and the measurements above are what replaces it.
