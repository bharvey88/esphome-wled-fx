# Round 2 fixes

What was verified, what changed, and what the evidence says now. One section
per finding, in the order `FINDINGS.md` puts them, then the tooling, then the
places where the critic was wrong.

Everything here was measured against the two device capture runs round 2 took,
`esphome-wled-fx-ref` and `esphome-wled-fx-ref2`, plus new captures taken
during this work: two full port runs of this build, a thirteen effect device
run at a thirty second window, two matched thirty second pairs of PS Box, and a
direct palette experiment on the device over its JSON API. The device was left
as it was found: on, brightness 220, preset 5, effect 53, all three colours
black, verified by reading `/json/state` back at the end.

Headline, for a reader in a hurry:

| Measurement | Round 2 reported | Now |
|---|---:|---:|
| What a HUB75 panel is sent for a buffer value of 128 | 128, WLED sends 56 | 56 |
| What it is sent for Matrix's spawn pixel | 215, WLED sends 175 | 175 |
| Fire 2012 on palette "Default" | Party | palette 35, as on the device |
| Effects that lose the user's palette when selected | 160 of 223 | 0 |
| Non-audio effects the comparison flags | 25 | 2 |
| The same comparison run against the device's own two runs | not measured | 24 |
| PS Box against the device, 30 s at matched controls | 0.79 at 6 s | 1.11 and 1.04 |

The two remaining flags are Color Clouds, which is deviation 28 and upstream's
uninitialised white channel, and Colorwaves at 1.1 times its noise floor.

---

## R2-1, the output gamma. Critic right, and it is the most user visible thing in this release

**Verified.** Every line of it. WLED gamma-corrects the finished frame in
`show()` (`refs/WLED/wled00/FX_fcn.cpp:1723`), gated on `gammaCorrectCol`,
which defaults to true (`wled00/wled.h:412`) at a gamma of 2.2
(`wled.h:414`); the bus layer adds nothing; brightness is applied after, by the
bus (`FX_fcn.cpp:1802`). The port's display front end defaulted that stage to
1.0, so round 1's in-effect pre-compensation had nothing to cancel it.

The reference device confirms the configuration as well as the source.
`GET /json/cfg` on it returns `light.gc` of `{"bri": 1, "col": 2.2, "val": 2.2}`:
colour gamma 2.2, brightness gamma off.

**One thing the finding did not check, and it decides whether the fix is
right.** Both sides also go through a panel driver. WLED's HUB75 builds define
`-D NO_CIE1931` (`refs/WLED/platformio.ini`, the shared `[hub75]` flags, and
the same line is in the Apollo `WLED-M1` fork the reference device runs), so
the driver adds no curve and gamma 2.2 is the only one. ESPHome's hub75 driver
applies **CIE1931** unless told otherwise
(`esphome__esp-hub75/include/hub75_config.h:77`). Defaulting this component to
2.2 and leaving the driver alone would have put two curves in series and made
the panel much darker than a WLED one, which is not what "matches a WLED
device" means.

**Changed.** `1e083f4`.

* `build_output_gamma_lut()` in `wf_color.cpp`, upstream's expression in
  float, built once at setup into the table the front end already had. No
  per-frame allocation and no per-frame `pow`.
* the display front end's `gamma_correct` defaults to 2.2.
* `examples/m1-hub75.yaml`, `m1-hub75-audio.yaml` and the two hub75 hardware
  test configs set `gamma_correct: LINEAR` on the display and `2.2` on
  `wled_fx`, with the reason in a comment.
* the four light configurations set the light's own `gamma_correct: 2.2`,
  against ESPHome's default of 2.8.
* config validation warns, naming both curves, when a hub75 display driven by
  `wled_fx` is left on a curve of its own underneath a `gamma_correct` above
  1.0. A warning and not an error: two curves is a look, and only the user
  knows whether they want it.
* `tools/snapshot/port.yaml` still pins 1.0 and now says why: the comparison
  is pre-gamma on both sides deliberately.

**Master brightness ordering, checked rather than assumed.** WLED gammas the
frame and lets the bus scale it afterwards. The display front end already did
exactly that (`wled_fx.cpp`, the brightness branch applies `scale_output_`
after the table), so nothing changed there. The light front end applies its
scale before `set_rgbw()` and ESPHome's own curve then bends it; that is not
fixable from this side, because the light owns the stage after this one, and it
is written down in deviation 33 with the numbers.

**What the two outputs emit now.** Buffer to LED driver, at full brightness:

| buffer | WLED HUB75 | this on HUB75, gamma_correct 2.2 plus LINEAR driver | this on a WS2812 light at 2.2 | ESPHome's light default of 2.8 |
|---:|---:|---:|---:|---:|
| 32 | 3 | 3 | 3 | 1 |
| 64 | 12 | 12 | 12 | 5 |
| 128 | 56 | 56 | 56 | 37 |
| 192 | 137 | 137 | 137 | 115 |
| 215 | 175 | 175 | 175 | 158 |

Matrix's spawn pixel is the last row: the effect builds it as
`gamma8inv(175)`, the engine holds (215, 255, 215) on both sides, and both
now send (175, 255, 175) to the panel.

A dimmed strip still differs, and no setting changes that: at half brightness
a buffer value of 128 leaves a WLED strip as `gamma2.2(128) * 128/255` = 28 and
an ESPHome light as `gamma2.8(64)` = 5.

**Regression check.** `ad6a05f`, in `wled_fx_effect_test`: the table at 2.2 is
`GAMMA_T` byte for byte, 128 maps to 56, 215 to 175, 1.0 is the identity and
2.8 is 37 at mid grey. This is the stage T14 pointed out that nothing in the
capture harness can see.

---

## R2-2, palette "Default". Critic right, and the mechanism is now in the segment

**Verified** against `refs/WLED/wled00/FX_fcn.cpp:234` and `:616-619`, and
against the device. The port had no `_default_palette` at all.

**Changed.** `91b8f1c`. `EffectDefaults` carries the declared palette as a
signed value, -1 meaning the metadata names none, and a `default_palette`
which is the declared one or 6, Party, exactly as `if (sOpt <= 0) sOpt = 6;`
says. `Segment` carries `default_palette`, initialised to 6 as upstream
initialises it, and `load_palette()` remaps a palette of 0 to it at the top,
where upstream does.

The other half of upstream's palette rule was already right and is left alone:
`color_from_palette()` returns the segment colour at palette 0 rather than a
palette entry, which is why Noise 1 at `pal=0` is solid amber on both sides.

**Evidence.** The critic's device probe, Fire 2012 at `pal=0` against `pal=35`
at a hue distance of 0.00, plus the new test over five effects that declare a
palette and one that does not.

**Regression check.** `ad6a05f`, `test_palette_defaults()`: for Fire 2012,
Glitter, Flow Stripe, Firenoise and Pacifica the palette loaded on "Default" is
the palette the effect declares, and for Ripple, which declares none, it is
Party.

---

## R2-3, the palette reset. Critic right about the behaviour, wrong about the count

**Verified on the device, directly.** With `fxdef: true` and transition 0, over
the JSON API:

| Step | Effect | Metadata | Device `pal` after |
|---|---|---|---:|
| 1 | Fire 2012 | `pal=35` | 35 |
| 2 | Ripple | none | 35 |
| 3 | Colortwinkles | none | 35 |
| 4 | Glitter | `pal=11` | 11 |
| 5 | Scanner | none | 11 |

That is `if (sOpt >= 0 && loadDefaults) setPalette(sOpt);` and nothing else.
The port reset all of them to 0.

**Changed.** `91b8f1c`. The engine writes the palette only when the metadata
names one. `m12` is reset to `M12_PIXELS` when it is not named and `si` is left
alone, which is upstream's treatment of those two, and both carry upstream's
`constrain()` into the parser, which the port had neither of. The select entity
follows the engine through the existing state change callback, so its published
state follows with it, and the hardware test harness refills controls by
reselecting the current effect, so it follows too.

**Where the critic is wrong.** The counts. "104 of 223 effects declare `pal=`"
and "112 of 216 reset the palette" are both too high: stock WLED 16.0.1
declares `pal=` on **59** of its 216 metadata strings and this build on **63**
of 223, the extras being WLED-MM effects. The 112 is a device readback taken
during a capture run, where the palette an effect inherits is whatever the
effect before it left, so it counts carried-over values rather than
declarations. The behaviour the finding describes is real and now proved
directly; the numbers in it are not. The correct count is in the test, which
fails if it moves.

---

## R2-4, the platform branches. Critic right on all three, and on the exposure

**Verified.** `CONFIG_IDF_TARGET_ESP32S2` is an `sdkconfig.h` symbol and
nothing in this component's include chain reaches that file; the generated
`compile_commands.json` for the M-1 build carries `-DUSE_ESP32`,
`-DUSE_ESP32_FRAMEWORK_ESP_IDF` and `-DUSE_ESP32_VARIANT_ESP32S3` and no
`-DCONFIG_*` at all. The branch could never be taken.

**Changed.** `1e083f4`. The ladder is upstream's table in upstream's order,
ESP8266 then S2 then PSRAM inside the `#else`, spelled with the defines ESPHome
really emits. `dump_config()` prints which profile the build took and the fair
share it derived, because `USE_PSRAM` means the configuration has a `psram:`
block and not that the board has PSRAM.

**ESP8266, decided from the code.** Not supported, and the Python schema says
so with a message rather than leaving an untested branch. WLED's own ESP8266
build gives one segment 384 bytes against the 4096 an ESP32-S3 gets, the engine
holds a full 32 bit canvas of its own on top of whatever the output needs, and
223 effect bodies is a lot of flash for a 1 MB part. The budget row is still
there, so the table is upstream's whole table and the `#ifdef ESP8266` effect
bodies have something consistent behind them if that ever changes. README says
it under known limitations.

**Regression check.** `tools/check_effect_names.py`, which the sweep and CI both
run, parses `refs/WLED/wled00/FX.h` and fails if `wf_segment.h` stops being
upstream's four rows in upstream's order.

---

## R2-5, the missing random draw. Critic right

**Verified** at `refs/WLED/wled00/util.cpp:662`. **Changed** in `7af3e49`:
`hw_random8()` is called and discarded where upstream draws it, and the beat
still sets the flag.

**Evidence it is fixed.** The critic's own test: the simulator's black
allowance for Fw Starburst audio goes back to 2 pixels, and the effect lights
up at three pixels in a 300 frame run again. Measured, not assumed:
`wled_fx_sim --effect "Fw Starburst audio" --size 3x1` is `ok` on both control
passes. Puddlepeak moves from 0.54 to 0.48 mean brightness, which is the same
sequence shift seen from the other side and is still two orders above the 0.01
it was before round 1.

---

## R2-6, the 1D collision bin clamp. Critic wrong

The reading was that `max(50, (usedParticles + 1) / 4)` can ask for more bin
slots than the array has, because upstream's `binIndices` is a stack array of
exactly that size while this port's is part of the particle system's
allocation.

It cannot. `calculateBinArrayEntries1D()`
(`components/wled_fx/wf_particle.cpp:1821`) sizes that array as
`max(50, (numparticles + 1) / 4)` rounded up to an even number, which is the
same expression, and `setUsedParticles()` multiplies `numParticles` by at most
256/256, so `usedParticles` never exceeds `numParticles`. The clamp is
unreachable.

Measured rather than argued: the new `test_collision_bins()` builds the 1D
particle system at thirteen strip lengths from 2 to 1000 pixels and seven
particle fractions and compares what the binning asks for against what the
array holds. Ninety-one combinations, headroom never negative, tightest zero.
The clamp stays where it is, as the safety net its comment says it is.

---

## R2-7, the deviation list. Critic right on all four, and on all three citations

All of it is in `23a1e21`. Deviation 27 now names Puddlepeak alone, records
that Waterfall is not peak-gated and that Ripple Peak was dimmed rather than
blank, adds the frame-rate dependence of upstream's rate and the restored
random draw. Deviation 30 stops claiming to be the only metadata difference and
names the seven MM strings that drop the trailing moon glyph, with the reason.
Deviation 31 gains the blanked seventh slider name. Deviation 33 is rewritten
around what reaches the LEDs on each of the three paths, including the
`gammaCorrectCol` gate this port pins true and the `gammaCorrectBri` stage it
has no equivalent of. The three citations are corrected:
`fastled_slim.cpp:105`, `MAX(3,SEGLEN/10)` at `FX.cpp:2168` with the one pixel
early return at `:2158`, and deviation 32's dark band narrowed to 2040 to 2047
and every 2048 after it.

---

## R2-8, the slider labels. Critic right

`SLIDER_LABEL_DEFAULTS[0]` and `[1]` are "Effect speed" and "Effect intensity",
which is what `refs/WLED/wled00/data/index.js:1646` restores for a `!` label.
`91b8f1c`, with the label test updated so it holds.

No ESPHome entity is renamed. The strings are what the "Effect controls" text
sensor publishes; the `name:` on every `number` platform in the examples is
still Speed and Intensity, so no entity id moves and no existing configuration
breaks. The release notes say so.

---

## PS Box: the window, again

Round 2 carried PS Box as the one defect-watch, at 0.79 of the device over
three runs. It is the same artefact PS Starburst turned out to be.

PS Box spawns **one particle per frame** ("only spawn one particle per frame
for less chaotic transitions", both sides), so the picture fills up over the
first few hundred frames and a six second window catches an arbitrary point of
that ramp. `mode_particlebox` was diffed statement for statement against
`refs/WLED/wled00/FX.cpp:8590-8674` first: identical, including the types on
`maxParticleSize`, `currentParticleSize` and the two gravity terms, the
`hw_random16()` seeding of `aux0`, and the washing machine and sloshing
branches.

Two matched pairs, both sides at 30 s, the port matched to the device's own
recorded controls:

| Pair | device | port | ratio | lit pixels a frame |
|---|---:|---:|---:|---|
| 1 | 18.47 | 20.43 | 1.11 | 138.7 / 150.2 |
| 2 | 17.86 | 18.63 | 1.04 | 122.0 / 133.3 |

Brightness quantiles over lit pixels agree within a few counts at every
quantile (device q50 121 and 139, port 125 and 131). The particle class noise
floor is 8 counts of mean brightness; the differences here are 2.0 and 0.8.
PS Box is clean, and `SLOW_EFFECTS` is the general answer: it now gives twelve
effects a thirty second window on both sides.

---

## Tooling

### T9, thresholds that were never checked against the instrument. Critic right

`NOISE_FLOORS` holds the p95 of the device against itself, per effect class,
with the run pair named in the comment, and every flag prints how far outside
its floor it is. The class comes from the registry's audio flags and from the
files that register the particle effects, not from a name prefix.

### T10, the hue distance. Critic right, and there was a second half

Hue is pooled the way brightness was: the score is the closest of every pairing
of runs and the report prints the widest. On top of that, a side's own hue
spread is a floor, which is NOISE.md's third rule applied to the metric that
needed it most. TV Simulator scores 1.00 against the port and 1.00 against
itself, so it is not a finding; PS Vortex, which agrees with itself to 0.1
counts, keeps a tight floor.

### T11, `frozen` and `all_black`. Critic right

Both are notes now unless the numbers behind them clear the class floor, and
the note prints the two numbers. The same treatment reached two things the
finding did not name: the motion direction word, which flips between two runs
of the same device on 22 of 216 effects, and "one side moves and the other is
still". Both need every run on one side to disagree with every run on the
other before they score, and neither is scored at all below 40 lit pixels a
frame, which is the guard the hue reading already had. PS Sparkler reads
0.0 px/s on the device and 4.9 on the port at a mean brightness of 0.3 on both.

### T12, the capture window. Critic right

`SLOW_EFFECTS` in `metrics.py`, twelve effects at thirty seconds, consulted by
both capture tools. The device tool takes the per-effect window directly; the
snapshot harness runs the slow ones as a second pass, because a worker holds
one window for its whole slice. The comparison refuses to score brightness,
coverage or frame change when the two sides' windows differ anyway, and says
why. `--match-reference` also implies the reference folder's effect list now,
so a thirteen effect reference folder produces a thirteen effect port run.

### T16, one run is not a measurement. Critic right

Both sides pool by default: a folder holding several run folders is several
runs. The report says, in the capture facts, when either side has only one.

### What the tool says now

| Comparison | Non-audio effects flagged |
|---|---:|
| round 2's tool, round 2's captures | 25 |
| this tool, round 2's captures of v0.3.1 | 2 |
| this tool, two fresh port runs of this build | 2 |
| this tool, the device's two runs against each other | 24 |

The 24 is the instrument measuring itself with one run a side, which is the
worst case the tool can be used in: nothing can be pooled, so no per-effect
spread suppresses anything, and neither run had the `SLOW_EFFECTS` window.
Thirteen of its flags are coverage, nine are brightness, seven are hue and
one is the frozen boolean, and the two worst are Sweep and Wipe at 10.4 and
7.6 times the coverage floor, which are exactly the effects that outrun a six
second window. It is the number that says how much of the remaining noise is
the capture path rather than the port, and it is why the report warns about a
side with a single run.

The two flags against this build are **Color Clouds**, which is deviation 28
and upstream's uninitialised white channel arriving through the live view, and
**Colorwaves**, 28 counts of mean brightness against a floor of 25.9, which is
1.1 times the floor on an effect whose brightness is driven by four beats
longer than the window. Sinelon Rainbow's direction word, the only other flag
against the v0.3.1 captures, does not reproduce with two runs of this build.

The twelve slow effects, captured at thirty seconds on both sides at matched
controls, flag three: Wipe Random at a hue distance of 1.00, which is a random
colour against another random colour, and Sweep Random and Sweep at 1.3 and
1.2 times the brightness and coverage floors.

---

## Where the critic was wrong, collected

1. **R2-6 is not a defect.** The 1D bin array is sized from the same
   `max(50, ...)` the binning asks for, so the clamp cannot fire. Ninety-one
   measured length and fraction combinations, headroom never negative.
2. **"104 of 223 effects declare `pal=`" and "112 of 216 reset the palette".**
   It is 59 of 216 upstream and 63 of 223 here. The larger number counts
   palettes carried over during a capture run, not declarations.
3. **R2-1 did not check the driver underneath.** WLED's HUB75 builds define
   `-D NO_CIE1931`; ESPHome's hub75 driver applies CIE1931 by default.
   Defaulting this component to 2.2 without turning that off would have
   overshot in the other direction.

Everything else in `FINDINGS.md`, `VERDICTS.md`, `NOISE.md` and `TOOLING.md`
held up.

---

## Builds and sizes

All eight configurations validate and compile on Windows against the ESPHome in
`C:\Users\bharv\esphome-venv`. The four hardware test firmwares were rebuilt in
`C:\tmp\wfx-flash` from the repository copies with `external_components`
repointed at the working tree and `.esphome` left alone, as before.

| Config | Flash v0.3.1 | Flash v0.4.0 | Delta | RAM v0.3.1 | RAM v0.4.0 | Delta |
|---|---:|---:|---:|---:|---:|---:|
| `strip-esp32.yaml` | 888,079 | 888,203 | +124 | 46,836 | 46,836 | 0 |
| `m1-hub75.yaml` | 882,827 | 882,815 | -12 | 110,019 | 110,019 | 0 |
| `m1-hub75-audio.yaml` | 936,915 | 936,907 | -8 | 118,931 | 118,931 | 0 |
| `strip-esp32-arduino.yaml` | 975,459 | 975,531 | +72 | 47,900 | 47,900 | 0 |

The four hardware test firmwares, which carry the tour harness and the
diagnostics on top of the engine, and which also gained the two new tour
groups this round:

| Config | Flash v0.3.1 | Flash v0.4.0 | Delta | RAM v0.3.1 | RAM v0.4.0 | Delta |
|---|---:|---:|---:|---:|---:|---:|
| `m1-test.yaml` | 1,035,679 | 1,036,151 | +472 | 113,567 | 113,567 | 0 |
| `m1-test-audio.yaml` | 1,095,703 | 1,095,943 | +240 | 123,855 | 123,855 | 0 |
| `matrix-test.yaml` | 1,023,531 | 1,024,103 | +572 | 50,220 | 50,220 | 0 |
| `strip-test.yaml` | 977,215 | 977,767 | +552 | 50,220 | 50,220 | 0 |

The gamma table is built at setup into the 256 byte buffer that was already
there, so the output gamma costs nothing but the `pow` loop; the palette work
is three fields and a branch.

The sweep is green under ASan and UBSan: 223 effects at six geometries in both
control passes, the four smallest geometries, all five 1D-to-2D mapping modes,
the audio and behaviour tests, the large geometries without the sanitizers, and
the three Python checks.

---

## Still open, and deliberately so

* **PS Sonic Stream**, for the third round. It needs music playing in the room
  and the device has sat in a quiet one for three rounds.
* **The nine WLED-MM effects**, which no device here runs. They are verified by
  reading and by the simulator sweeps only.
* **Fw Starburst audio's budget**, sized from core WLED's table where WLED-MM's
  own would give 136 stars. Nothing can settle which is right without an MM
  device.
* **Colorwaves at 1.1 times its floor**, which is either the four beats longer
  than the capture window or something real, and a third port run would say
  which.
