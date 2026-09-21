# Hardware testing session guide

Nothing in this repository has ever run on hardware. This is the plan for the
session that changes that: four firmwares, a tour mode that walks every effect
so you are not typing names into a select box 223 times, and a table to fill in
as you go.

[HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md) is the list of open questions.
This document is how to answer them in one sitting.

Everything here is reachable from a browser on the LAN. Home Assistant is
optional and nothing in the session needs it.

## The first ten minutes

Flash [examples/hardware-test/m1-test.yaml](../examples/hardware-test/m1-test.yaml).
It is the 64x64 HUB75 build with all 223 effects, the tour and the diagnostic
sensors, and nothing else in the session means much until the panel and the
frame clock are known good.

There is nothing to fill in first. None of the four firmwares carries a wifi
network, an API key or an OTA password, so no `secrets.yaml` is needed to build
or to flash. Run this yourself from a PowerShell prompt with the ESPHome
virtual environment active, with the M-1 on USB:

```
cd examples/hardware-test
esphome run m1-test.yaml
```

Then give the board your network, once. Two ways, and either is fine:

**From a phone or a laptop.** The board has no network to join, so it brings up
its own access point at boot and keeps it up. Join **WLED FX M-1 test**. It is
open, with no password. The captive portal page opens on its own on both
Android and iOS; if it does not, browse to `http://192.168.4.1/`. Pick your
network from the list, type the password, press save. The board writes the
credentials to flash and joins straight away, with no reboot needed, and it
loads them again on every boot after that.

**Over the USB cable.** The firmware also has Improv over serial, so from a
Chromium browser (Chrome or Edge) you can open
[the ESPHome web tools page](https://web.esphome.io/), connect to the same
serial port you just flashed from, and enter the network there. Nothing else
can be using the port at the time, so close `esphome logs` first. This works on
both the M-1 and the plain ESP32 boards.

Once it has joined, the device is at `http://wled-fx-m1-test.local/` and the
access point goes away.

A healthy boot looks like this. The canvas is the panel size, the frame
interval is 23 ms, the effect count is the whole port, and the tour publishes
its first line at the end of setup:

```
[C][wled_fx:139]: WLED FX display:
[C][wled_fx:139]:   Canvas: 64x64
[C][wled_fx:139]:   Frame interval: 23 ms
[C][wled_fx:139]:   Effects compiled in: 223
[C][wled_fx:139]:   Effect: Fire 2012
[C][wled_fx:139]:   Palette: Fire
[I][tour:049]: [tour] 12/223 "Fire 2012" group=1d_a
[D][sensor:094]: 'Effect frame rate': Sending state 43.2 fps
```

A canvas that is not 64x64 means the display size is not reaching the engine,
and that is a stop-and-fix before anything else.

Three numbers to write down before you go any further, all of them on the web
interface at `http://wled-fx-m1-test.local/` and all of them in the log:

1. **Effect frame rate** on the starting effect, once it has settled. This is
   the baseline every later reading is compared against.
2. **Heap free** at boot, before you start the tour.
3. **Effect frame rate** on `PS Galaxy`, which is the heaviest thing in the
   port. Set **Tour group** to `Particle 2D` and step to it, or pick it from
   the Effect select.

If the first and the third are both near 43, the frame clock is right and the
panel is keeping up, and the rest of the session is about how things look
rather than whether they run.

## Before you start

The configs live in [examples/hardware-test](../examples/hardware-test).

1. Activate your ESPHome virtual environment. On Windows, do it from PowerShell
   and not from Git Bash: the ESP-IDF toolchain installer refuses to run under
   MSys.
2. On Windows, if a build dies on a path length, copy the `hardware-test`
   folder somewhere short such as `C:\tmp\wfx-hw`, change `external_components`
   to an absolute path to the repository's `components` directory, and build
   there instead.

No secrets file is involved. The shared network setup is in
[network.yaml](../examples/hardware-test/network.yaml), which every one of the
four configs includes as a package, and it has no credentials in it at all.

Each board offers its own access point, named after the config, so two of them
can sit on the bench at the same time without colliding:

| Config | Access point | Address once it has joined your network |
|---|---|---|
| `m1-test.yaml` | WLED FX M-1 test | `http://wled-fx-m1-test.local/` |
| `m1-test-audio.yaml` | WLED FX M-1 audio test | `http://wled-fx-m1-audio-test.local/` |
| `strip-test.yaml` | WLED FX strip test | `http://wled-fx-strip-test.local/` |
| `matrix-test.yaml` | WLED FX matrix test | `http://wled-fx-matrix-test.local/` |

All four are open access points with no password, and all four portals live at
`http://192.168.4.1/` if the page does not pop up by itself.

**Moving a board to another network later.** Nothing to reflash. A board that
cannot join the network it has saved brings its access point back up after
about 90 seconds, so take it somewhere else, wait, join the access point and
put the new network in. Improv over serial works at any time as well and
overwrites whatever is saved.

**What is deliberately missing.** There is no API encryption key and no OTA
password in these builds, because either one would mean writing a secrets file
before the first build. These are throwaway test firmwares on a home LAN.
`esphome logs` over wifi, OTA reflashing and Home Assistant adoption all work
without them. If you want them back, uncomment the two blocks in `network.yaml`
and copy `secrets.yaml.example` to `secrets.yaml` in that folder. Every
`secrets.yaml` in the tree is gitignored, so it cannot be committed by
accident.

**Nothing reboots on its own.** The API's reboot timeout and wifi's are both
set to `0s` here. A browser only session with no Home Assistant attached leaves
the board running, which the ESPHome defaults would not: the API's default
reboots after 15 minutes of nothing connecting to it, and that looks exactly
like a firmware crash when you are three effects into a tour.

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
has a connection. The captive portal does not: its page is compiled into the
firmware, so the onboarding step above works with no internet at all.

## How the tour works

Turn on **Tour auto advance** and the device steps to the next effect every
**Tour dwell** seconds, forever, wrapping at the end. Turning the switch off
pauses on whatever is showing. Every change logs one line:

```
[I][tour:049]: [tour] 37/223 "PS Vortex" group=particle_2d
```

The two numbers are the position in the current filter and the size of that
filter, so with **Tour group** set to `All` they count to 223.

The tour only ever walks effects the output can actually run, because the
component will not select the others and the **Effect** select does not list
them. All four test firmwares set `include_1d_effects: true` on purpose, which
is what makes `All` mean 223 on the two panel builds and the matrix build; a
panel firmware you would actually want to look at should leave that out and
offer the 64 effects written for a matrix.

That opt-in is why the panel and matrix firmwares start on the **Panel** group
rather than on `All`. A 1D effect on a 64x64 panel is drawn on a 4096 pixel
strip wrapped across the panel, so `Gradient` is a short line crawling along
it: exactly what WLED does, and not what a panel looks like when it is working.
**Panel** is the 64 effects a 2D output offers without the opt-in, read from
the same rule the component itself applies, and **Strip** is the other side of
it, which is what `strip-test.yaml` starts on. Switch to `All` whenever you
want the mapped ones; the **Effect group** sensor marks them
`(strip effect, mapped)` so it is clear which is which, and **Profile run**
always covers all 223 whatever group you were on.

The **Effect** dropdown follows the same rule. On the three matrix builds it
lists those same 64 and nothing else, so scrolling it cannot land you on a
crawling strip effect without meaning to; the other 167 are on a second select
called **Strip effect (mapped)**, under Config, which says in its name what
picking one will look like. While one of those is running, **Effect** holds its
last value rather than showing an option it does not have, and **Effect name**
is always what the engine is actually running.

If you saw a strip effect on the panel with the group reading `Panel`, that is
what it was: the **Panel** group has never contained one. Either the dropdown
was used, or the firmware predates the group. A host test now pins the group at
exactly 64 effects with `Gradient` and `Fireworks Starburst` outside it
(`tools/sim/effect_test.cpp`, "what each output shape offers"). On `strip-test.yaml`, a real one
dimensional strip, `All` is 167 and the `2D` and `Particle 2D` groups are
empty: picking one logs

```
[W][tour:084]: [tour] this group has no effect that runs on a 1D output
```

and leaves the current effect on screen. That is the rule working, not a fault.
[The README](../README.md#which-effects-an-output-offers) has the whole of it.

| Entity | What it does |
|---|---|
| Tour auto advance | On steps forward on a timer, off pauses. |
| Tour dwell | Seconds per effect, 1 to 120. Eight is a sensible first pass. |
| Tour group | `All`, `Panel`, `Strip`, `1D`, `2D`, `Particle 2D`, `Particle 1D`, `Audio`, `MM`, `Checklist`. Panel builds start on `Panel` and the strip build on `Strip`. |
| Next effect / Previous effect | Step by hand. Both respect the group filter. |
| Restart effect | Back to frame zero without changing effect. This is the one to use on the startup transient questions, PS Galaxy and Blobs. |
| Pin controls | Off by default. See [Controls](#controls). |
| Unpin controls | Puts Speed, Intensity, Custom and Check back on the effect's own defaults, including anything the YAML pinned. |
| Profile run | One unattended timed pass over every effect. See [Profile run](#profile-run). |
| Effect | The component's own effect select. On the three matrix builds it lists the same 64 the **Panel** group walks, so a 1D effect cannot be picked here by accident; on `strip-test.yaml` it lists all 167 a strip can run. It follows the engine, so it tracks the tour as it moves, and it holds its last value while a mapped strip effect is running. |
| Strip effect (mapped) | Matrix builds only, under Config. The 167 effects a strip can run, drawn here through WLED's 1D to 2D mapping. This is the deliberate way to reach `Gradient` or `Fireworks Starburst` on a panel. |
| Palette | The component's palette select, all 72. |
| Speed, Intensity, Custom 1 to 3 | The component's numbers. |
| Check 1 to 3 | The component's switches. |
| Color 1, Color 2, Color 3 | The three WLED colour slots, one colour picker each. |
| Effect controls / Effect colours | Text sensors naming what the controls above do in the running effect. |
| Panel brightness | hub75 brightness, panel builds only, starting at **220**. That is where the reference WLED device sits, not the driver's own default of 128: a side by side at 128 against 220 measures the duty cycle and tells you nothing about the engine. This is the one to dim with, because it shortens the drive time rather than scaling an 8 bit frame. On the strip builds it is the light entity's own slider. |
| Effect name / Effect group | Text sensors mirroring the current effect. The group reads `(strip effect, mapped)` after the group name when a 1D effect is being shown on a panel. |
| Effect render time / Frame output time | Microseconds per frame. See [Profile run](#profile-run). |

`Checklist` is the group worth knowing about: it filters the tour down to the 30
effects [HARDWARE-CHECKLIST.md](../HARDWARE-CHECKLIST.md) actually asks about.
One pass of that with a long dwell is the fastest route through the table below.

None of the control entities survive a reboot. A reflash puts everything back to
what the YAML says.

## Controls

WLED gives every effect the same eight controls and then relabels them per
effect, so "Custom 1" is Trail on Matrix, Fuse on PS Fireworks and nothing at
all on Metaballs. The page cannot rename its own entities, so it publishes the
labels instead, in two text sensors that change every time the effect changes.

**Effect controls** names the controls the running effect actually uses, in
WLED's own words, which is why the first two read `Effect speed` and
`Effect intensity` where the entities beside them are named Speed and
Intensity. For Matrix:

```
Effect speed · Effect intensity: Spawning rate · Custom 1: Trail · Check 1: Custom color
```

Read it as: the Speed entity does what speed always does, the Intensity slider is the
spawning rate, Custom 1 is the trail length, Check 1 turns the custom colour on.
Custom 2, Custom 3, Check 2 and Check 3 are not listed, so on this effect they
do nothing at all. For Fire 2012 the same sensor reads
`Effect speed: Cooling · Effect intensity: Spark rate · Custom 2: 2D Blur · Custom 3: Boost`,
and for PS Fireworks it names all eight.

**Effect colours** does the same for the palette and the three colour slots.
For Matrix:

```
Color 1: Spawn · Color 2: Trail
```

Matrix does not use the palette, so the palette is not mentioned, and it has no
third colour. An effect that does use the palette says so first, and a slot the
effect did not name falls back to WLED's own words for it: `Fx` for the effect
colour, `Bg` for the background, `Cs` for the custom one.

The controls themselves:

| Control | Range | Notes |
|---|---|---|
| Speed | 0 to 255 | |
| Intensity | 0 to 255 | |
| Custom 1, Custom 2 | 0 to 255 | |
| Custom 3 | **0 to 31** | Five bits in WLED, and the effects divide it down as one. On Scrolling Text it is the glyph rotation and 16, the default, is upright. |
| Check 1 to 3 | on / off | |
| Color 1, Color 2, Color 3 | colour picker | |

### The colour inputs

The three colour slots are three ESPHome lights, because a light is the only
entity type that both web_server and Home Assistant render as a colour picker.

* **Color 1** is the effect colour and also the panel master, as it is in WLED:
  its brightness slider dims the whole finished frame and turning it off blanks
  the panel. The colour it holds is taken at full brightness, so a dim panel is
  not also a washed out colour 1. It comes up on at WLED's own default primary,
  so adding it changes nothing about how any effect looks.
* **Color 2** and **Color 3** are plain colour slots. Their brightness dims that
  colour and turning one off makes it black, which is how you tell an effect it
  has no background or no custom colour. Both come up off, which is what the
  engine starts with anyway.

On the two strip builds the light front end takes colour 1 from the same entity
rather than from the strip light's own colour, so the harness behaves the same
everywhere. The strip light's own on/off and brightness still work, so Color 1's
on/off is ignored there.

In YAML the same three slots are the `wled_fx.set_color` action:

```yaml
- wled_fx.set_color:
    id: fx
    color: 2      # 1, 2 or 3
    red: 255
    green: 170
    blue: 0
```

### Pin controls

Off, which is the default, a control belongs to the effect you set it on: the
next effect change refills every control from that effect's own WLED defaults
and republishes them, so each effect starts the way its author meant.

On, a control you move follows you into every effect afterwards. That is the
component's own behaviour and what a `speed:` in YAML does. It is useful when
you are deliberately comparing one setting across effects and baffling when you
have forgotten you left it on, which is why it is a switch you can see.

Turning the switch off does not unpin what is already pinned, including anything
the YAML set. **Unpin controls** does that, and also puts the running effect
back on its own defaults.

This matters more than it sounds. Scrolling Text reads Intensity as a Y offset,
and at exactly 0 or 255 it stops scrolling across the panel and sweeps up or
down it instead. An Intensity of 255 left over from an earlier effect is enough
to make it look broken.

## What to watch in the logs

* `[tour]` lines. This is your index into everything else. When something looks
  wrong on the panel, the preceding `[tour]` line names it.
* `[wled_fx]` at boot. `dump_config` prints the canvas size, the frame interval,
  how many effects were compiled in and the starting effect and palette. If the
  canvas is not 64x64 then the display size is not reaching the engine.
* `Effect frame rate`. Published every two seconds. It counts frames the engine
  really rendered, not the interval it was asked for. At the default 23 ms
  interval the ceiling is about 43, on both front ends, and a light effect
  should read the same as the panel does.

  The review changed how that works, which matters for reading the number. The
  frame gate used to re-arm from the moment the frame ran, and with ESPHome's
  main loop ticking about every 16 ms a 23 ms request became 32 ms and
  everything ran at 31 fps regardless of how heavy it was. The gate accumulates
  its deadline now: the period alternates between one tick and two and the
  average comes out at 23 ms. So a reading near 31 is no longer the scheduler,
  it is the effect genuinely needing two main loop ticks per frame.

  The sensor publishes nothing for a window in which the effect changed, so
  during a fast tour it reports less often. That is deliberate: the frame
  counter restarts at zero on every effect change, and a part window divided by
  a whole window is a low number for the wrong reason. Pause the tour before
  you write a figure down.
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

## The four things most likely to need a decision

Everything below is a measurement. These four are the ones where the answer is
probably "change something", so they are worth doing before the long tours.

1. **Microphone gain and squelch.** `gain: 60` and `squelch: 10` are WLED's
   numbers for WLED's input path, not for an INMP441 on an ESP32-S3 through
   ESPHome's I2S. Both are compile time, so tuning them means editing
   `m1-test-audio.yaml` and reflashing, which is why the level instrumentation
   exists: get the readings first, change once. Item 3.1 in the table below.
2. **Whether a 64x64 panel finishes a frame inside 23 ms.** The frame gate now
   really does ask for 23 ms, which it never did before the review, and nothing
   has measured what a full 4096 pixel canvas plus a hub75 blit and flip
   actually costs on the heavy effects. The frame rate table is this question.
3. **Heap behaviour over days.** The effect scratch block grows and is reused
   rather than being freed on every effect change, which is a deliberate
   divergence from WLED aimed at fragmentation on a board with no PSRAM. Three
   tours is enough to catch a leak. It is not enough to know whether the
   grow-and-reuse policy settles or creeps, and that needs a device left
   cycling for a few days.
4. **The FFT task beside a live render loop.** The analysis runs on its own
   task while hub75 is driving the panel from an interrupt and the main loop is
   rendering. The failure modes are tearing, dropped frames and a watchdog
   reset, none of which a simulator can show. Item 3.9.

## Numbers to write down

### Frame rate at 64x64

A [profile run](#profile-run) measures all 223 of these in one unattended pass
and is the better way to fill this table in. Doing it by hand: pause the tour on
each of these, let it settle for ten seconds, then read `Effect frame rate`.
These are the heaviest things in the port: the particle systems, the per pixel
noise fields and the cellular ones.

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

If the panel cannot hold 23 ms on the effects you care about, `update_interval`
on the `wled_fx:` block is the lever. Raising it to `33ms` asks for 30 fps and
gives the loop a third more time per frame. It is not free: effects that pace
themselves against `seg.now`, which is most of them, still look right, and the
ones that count frames run slow in proportion. Change it in one place, reflash,
and put the new number in the result column beside the old one rather than
replacing it. The same key exists on the light effect, under the `wled_fx`
entry in the light's `effects:` list.

### Profile run

One button, one unattended pass, one line per effect. **Profile run** sets the
dwell to 5 seconds, the group to `All`, goes to the first effect, turns auto
advance on and logs `[tour] PROFILE START`. Every time it moves on it writes the
timings for the effect it is leaving, and when it comes back round to the first
effect it logs `[tour] PROFILE DONE` and stops. All 223 effects at 5 seconds
each is about twenty minutes, a little more because two slow-paced effects,
Sunrise and PS Galaxy, are given six times the dwell.

Each line looks like this:

```
[tour] RESULT idx=12 name="Fire 2012" group=1d_a frames=172 fps=43.5 render_us=4210/3980/9600 out_us=1480/1400/2900 heap=201344 largest=110592 psram=4063232 data_bytes=4096
```

`render_us` and `out_us` are average, minimum and maximum microseconds, over
the time that effect was on screen with its first second dropped. The first
second is allocation and the first pass of a noise field, not the steady state.
Render time is the effect function; output time is everything from the canvas to
the panel. Solid at 43.5 fps with a low render time means the frame clock and
the panel are fine and any slow effect is slow in the effect.

The same two numbers are live on the page the whole time, as **Effect render
time** and **Frame output time**, if you would rather watch than capture.

Capture it from PowerShell, over wifi, with no serial port involved:

```
cd examples\hardware-test
C:\Users\bharv\esphome-venv\Scripts\esphome.exe logs m1-test.yaml --device wled-fx-m1-test.local | Out-File -Encoding utf8 C:\tmp\wfx-flash\profile-m1.log
```

Press **Profile run** on the web page once the log is streaming, leave it, and
press Ctrl+C after `PROFILE DONE`. Use `| Out-File -Encoding utf8` rather than
`*>` or `>`: Windows PowerShell 5.1 writes UTF-16 when it redirects, which most
tools then read as a wall of NUL bytes. The parser below copes with either, so
a log captured the other way is not wasted.

Then turn the log into a table:

```
cd C:\Users\bharv\development\esphome-wled-fx
python tools\parse_profile_log.py C:\tmp\wfx-flash\profile-m1.log --csv C:\tmp\wfx-flash\profile-m1.csv
```

It prints markdown, slowest first, with a `Slow` column marking anything under
40 fps, and writes the same rows to CSV. `--slow-only` drops everything that is
already fast. Unrelated log lines and the console colour codes are ignored, so
the whole session log can go in as it is.

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
readings, the allocation story is fine for a session.

Three tours does not settle the grow-and-reuse policy, though. The scratch
block is kept across an effect change and only reallocated when a bigger one is
asked for, so the interesting number is where it stops growing and whether the
largest free block is still falling once it has. That takes a device left
running with **Tour auto advance** on for a few days. Read the same four
columns once a day and add a row. It is worth doing on the M-1, which has
PSRAM, and on the plain ESP32 running `strip-test.yaml`, which does not, since
avoiding fragmentation without PSRAM is the reason the policy exists.

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
| 2.15 | Octopus | Above 180 pixels wide the radius scale floors to zero and the effect goes flat. Acceptable, or worth diverging from upstream | |
| 2.16 | Game Of Life | Above about 16384 pixels with coprime dimensions the spaceship check stops firing, so a stable glider is never reset | |

Tetrix and Rolling Balls want a non-square panel. If you only have the 64x64,
set `panel_height: 32` in `m1-test.yaml` and reflash rather than skipping them,
and say in the result which geometry you used.

2.15 and 2.16 are the extreme geometry pair, and a 64x64 panel cannot show
either one: both need a canvas wider than 180 pixels. If you have a chain of
panels, set `panel_width` and the chain keys in `m1-test.yaml` to something
past 180 and look at them there. If you do not, say so in the result rather
than leaving it blank, because "not reproducible on the hardware available" is
a real answer and stops the next person looking for a panel. Both are
upstream's arithmetic unchanged and both degrade to a duller animation rather
than crashing, so neither blocks anything.

Both are in the `Checklist` tour filter, so a pass of that group visits them
along with everything else.

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

### Section 4, frame rate

The frame rate table under "Numbers to write down" is this section. Fill that
in and the section is answered. The one extra thing it asks for, which the
table does not have a column for, is whether the light front end starves the
rest of the loop on a long strip: watch `Loop time` on `strip-test.yaml` with a
heavy effect running and compare it against the same effect on the panel.

### Section 5, build configurations

These two are not covered by the hardware-test configs. They use the existing
examples and are only worth doing if the hardware is free.

| # | Item | Result |
|---|---|---|
| 5.1 | `examples/strip-esp32-arduino.yaml`, esp-dsp under the Arduino framework | |
| 5.2 | `examples/host.yaml`, which does not build on Windows | |

### Section 6, memory over a long uptime

The multi-day rows under "Heap across a full tour" are this section. It is the
one item that cannot be finished in a single sitting, so start the device
cycling at the beginning of the session and come back to it.

### Light front end on a 2D canvas (`matrix-test.yaml`)

Not in the checklist, so these are lettered rather than numbered, but it is a
separate code path from the panel and nobody has watched it either.

| # | Item | Result |
|---|---|---|
| L1 | Scrolling Text reads left to right with no row mirroring | |
| L2 | Matrix falls downwards, not sideways | |
| L3 | A 1D effect expanded onto the 16x16 canvas looks like it does on the strip | |
| L4 | Frame rate at 16x16 on the heaviest particle effects | |

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
