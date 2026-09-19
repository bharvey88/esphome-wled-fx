# esphome-wled-fx

The WLED 16.0.1 LED effect engine, ported to ESPHome as an external component.

It runs WLED's effects on an ESPHome device with no WLED firmware, no FastLED and
no Arduino dependency, on plain addressable strips through the light stack and on
matrix panels such as HUB75 through the display stack.

This is the foundation release. Ten representative effects are ported so far; the
engine, palettes and the whole drawing surface the remaining effects need are all
in place.

## What works today

* WLED's segment model, 32 bit lossless framebuffer, palettes and colour maths
* All 72 WLED palettes: the dynamic ones, the seven FastLED ones and the 59
  cpt-city gradients
* Effects keyed by name, with their WLED defaults read out of the original
  metadata strings
* A compile-time allow-list, so a build only carries the effects it uses
* Addressable light effect front end, for any `light::AddressableLight` driver
* Display front end, one bulk frame push per tick, hub75 aware
* Runtime control from Home Assistant: `select`, `number` and `switch` platforms
  plus actions
* A host simulator that renders every effect to a PNG contact sheet
* A real audio source for the audio reactive effects: microphone, FFT and AGC,
  ported from WLED's `audioreactive` usermod, with simulated sound as the
  fallback when no microphone is configured

Ten effects: Solid, Blink, Rainbow, Pride 2015, Fire 2012, Noise 1, Plasma,
Matrix, Metaballs, Scrolling Text.

Not here yet: the particle system, the audio reactive effects, and the other
206 effects. See `PLAN.md`.

## Quick start, addressable strip

```yaml
external_components:
  - source: github://bharvey88/esphome-wled-fx

# A bare wled_fx block is what puts the effect into the light effect registry.
wled_fx:

light:
  - platform: esp32_rmt_led_strip
    id: strip
    name: Strip
    pin: GPIO16
    num_leds: 60
    rgb_order: GRB
    chipset: WS2812
    effects:
      - wled_fx:
          name: WLED FX
          effect: Pride 2015
```

## Quick start, HUB75 matrix

The engine owns the frame clock, so the display must be handed over with
`update_interval: never` and `auto_clear_enabled: false`. That is enforced at
codegen time.

```yaml
display:
  - platform: hub75
    id: matrix
    board: apollo-automation-m1-rev6
    panel_width: 64
    panel_height: 64
    update_interval: never
    auto_clear_enabled: false

wled_fx:
  id: fx
  display_id: matrix
  update_interval: 33ms
  effect: Fire 2012

select:
  - platform: wled_fx
    wled_fx_id: fx
    type: effect
    name: Effect
```

Full configurations are in `examples/`.

## Configuration

### `wled_fx:`

Accepts one entry or a list of them. An entry with no `display_id` is just the
registration stub that makes the light effect available.

| Option | Type | Default | Notes |
|---|---|---|---|
| `id` | id | generated | needed to point a select, number or switch at it |
| `display_id` | id | none | the display to drive; makes this a display front end |
| `effects` | list of names | all | compile-time allow-list, see below |
| `width` / `height` | int | from the display | canvas size override |
| `update_interval` | time | `33ms` | one rendered frame per interval |
| `gamma_correct` | float | `1.0` | applied on the way to the display only |
| `effect` | name | first registered | |
| `palette` | name | effect default | |
| `speed`, `intensity` | 0 to 255 | effect default | |
| `custom1`, `custom2` | 0 to 255 | effect default | |
| `custom3` | 0 to 31 | effect default | |
| `check1`, `check2`, `check3` | bool | effect default | |
| `text` | string | empty | for the text effects |

### The light effect

```yaml
effects:
  - wled_fx:
      id: strip_fx          # optional, for the select/number/switch platforms
      name: WLED FX
      width: 16             # for a matrix wired as one strip
      height: 16
      serpentine: true
      use_light_color: true # the light's own colour becomes segment colour 1
      update_interval: 33ms
      # plus every control key from the table above
```

### Audio reactive effects

WLED's audio reactive effects read one shared analysis struct: a smoothed volume,
a 16 channel graphic equaliser, a dominant frequency and a beat flag. With no
microphone configured they run on WLED's own `simulateSound()`, so every audio
effect animates out of the box and nothing has to be set up to try them.

Point `audio:` at a microphone and they run on real sound instead. This is a port
of WLED's `audioreactive` usermod: the same pre-filter, the same 512 point FFT,
the same GEQ bin mapping with its pink noise correction, the same AGC presets and
the same post-processing.

```yaml
microphone:
  - platform: i2s_audio
    id: board_mic
    i2s_audio_id: i2s_mic_bus
    i2s_din_pin: GPIO12
    adc_type: external
    channel: left
    bits_per_sample: 32bit
    sample_rate: 22050

wled_fx:
  id: fx
  display_id: matrix
  effect: GEQ
  audio:
    microphone: board_mic
    agc: normal
```

The block sits on a `wled_fx` entry, but the analysis source is process wide:
there is one canvas and one segment, so one microphone serves both front ends.
An entry that carries nothing but `audio:` is the normal way to give the light
effect a microphone, and only one entry may carry the block.

| Option | Type | Default | Notes |
|---|---|---|---|
| `microphone` | microphone source | required | `microphone`, `bits_per_sample`, `channels`, `gain_factor`, exactly as `sound_level` takes them. One channel of 16 bit audio |
| `passive` | bool | `false` | leave the microphone to another component to start and stop, and only listen in |
| `gain` | 0 to 255 | `60` | WLED `sampleGain`, the manual input gain. Ignored while AGC is on |
| `squelch` | 0 to 255 | `10` | WLED `soundSquelch`, the noise gate |
| `input_level` | 0 to 255 | `128` | WLED `inputLevel`. With AGC on it is the post-amplifier on the GEQ channels |
| `agc` | `off`, `normal`, `vivid`, `lazy` | `normal` | WLED's three AGC presets, a PI controller over the input level |
| `scaling` | `none`, `log`, `linear`, `sqrt` | `sqrt` | WLED `FFTScalingMode`, how a GEQ channel maps to 0 to 255 |
| `limiter` | bool | `true` | WLED's dynamics limiter on the smoothed volume |
| `attack` / `decay` | time | `80ms` / `1400ms` | limiter attack and decay, and the decay picks the GEQ fall-off curve |
| `mic_filter` | bool | `false` | an IIR band pass, 90 Hz to 20 kHz, over the raw samples |
| `bandpass` | bool | `false` | WLED's alternative bin mapping that ignores everything under 100 Hz |
| `task_in_psram` | bool | `false` | put the analysis task's stack in PSRAM |

Notes worth knowing before you tune it:

* **Sample rate.** 22050 Hz is what the usermod assumes and is the best match for
  its bin mapping. Any rate the microphone supports works: the GEQ bin ranges are
  rescaled from upstream's 22050 Hz reference so each channel keeps the frequency
  band WLED gave it, and the dominant frequency is reported in Hz either way.
* **The noise gate is aggressive.** Upstream's gate zeroes the filter state, not
  just its output, so a signal has to be roughly five times `squelch` before
  anything passes at all. If everything stays dark, lower `squelch` before
  reaching for `gain`.
* **Prefer `agc` over `gain_factor`.** The microphone source's `gain_factor` is a
  plain integer multiplier applied before the analysis; the AGC is a controller
  that tracks the room.
* ESP32 only, because ESPHome's `microphone` is.

The FFT runs on its own FreeRTOS task, never in the render loop, and every buffer
it uses is allocated once at setup. On ESP32 it uses `espressif/esp-dsp`, which
ESPHome already pins, so this adds no new third party dependency: the ESP32
assembly kernel on a plain ESP32, the S3 one on an S3, and the library's ANSI C
one elsewhere. The host simulator, and any target without esp-dsp, falls back to a
small radix-2 transform in `wf_fft.cpp`, and the analysis itself is identical
either way.

Enabling audio on an ESP32 costs about 55 to 60 KB of flash and 9 KB of static
RAM, most of it the `microphone`, `i2s_audio` and `audio` components rather than
the analysis, plus a 12 KB task stack and an 11 KB ring buffer at runtime.

### Trimming the build

Listing effects compiles only those in. On an ESP32 the ten-effect build is about
11 KB of flash over the same firmware without the component; cutting it to two
effects gets about 9 KB of that back.

```yaml
wled_fx:
  effects:
    - Fire 2012
    - Rainbow
```

Names are the WLED display names, case insensitive. A name that matches nothing is
a configuration error.

### Platforms and actions

```yaml
select:
  - platform: wled_fx
    wled_fx_id: fx
    type: effect      # or palette
    name: Effect

number:
  - platform: wled_fx
    wled_fx_id: fx
    type: speed       # intensity, custom1, custom2, custom3
    name: Speed

switch:
  - platform: wled_fx
    wled_fx_id: fx
    type: check1      # check2, check3
    name: Check 1
```

Actions: `wled_fx.set_effect`, `wled_fx.next_effect`, `wled_fx.set_palette`,
`wled_fx.set_text`, `wled_fx.set_speed`, `wled_fx.set_intensity`,
`wled_fx.set_custom1` to `set_custom3`, `wled_fx.set_check1` to `set_check3`.

## Host simulator

Renders every effect at 16x16, 64x64 and 60x1, checks the framebuffer guard bands
after every frame, and writes a PNG contact sheet per effect.

```
cmake -S tools/sim -B tools/sim/build -G Ninja
cmake --build tools/sim/build
tools/sim/build/wled_fx_sim --out tools/sim/out
```

`--effect NAME`, `--group NAME`, `--palette N`, `--frames N`, `--no-images` and
`--list` narrow it down. `-DWLED_FX_SANITIZE=ON` adds ASan and UBSan on toolchains
that have them.

The same build produces `wled_fx_audio_test`, which feeds synthetic PCM through
the audio processing core, the same code the firmware runs, and checks the
answers: a 1 kHz sine lights the right GEQ channel and reports the right
frequency, a sweep walks up the channels, a kick drum pulse train fires the beat
flag, silence and anything under the squelch stay dark, the AGC presets wind the
gain up and down, and the bin mapping still works at 16000, 32000 and 44100 Hz.
Add `--verbose` to see every band.

## Contributing an effect

Read `PORTING.md`. It has the exact transform table, the file conventions, how the
metadata string is parsed, the traps, and the workflow for porting a batch of
effects in parallel with other people.

`BATCHES.md` is the work breakdown: it assigns every WLED 16.0.1 effect that is not
ported yet to exactly one batch, one translation unit and one branch, so the
batches cannot collide.

## Licence

GPL-3.0-or-later.

The effect engine, palettes, colour maths and effect bodies are derived from
[WLED](https://github.com/wled/WLED) at v16.0.1, which is licensed under the
EUPL v1.2 or later. EUPL-1.2 Article 5 allows a derivative that is combined with a
work under a compatible licence to be distributed under that compatible licence,
and the EUPL Appendix lists GPL-3.0. Every derived file carries its own header
naming the upstream file and authors.

Parts of the scaling, wave and colour container code originate in
[FastLED](https://github.com/FastLED/FastLED) 3.6.0 and stay under the MIT licence,
with the notice reproduced in `components/wled_fx/wf_math.h`.

The bitmap fonts come from WLED's console fonts, credited upstream to
[raster-fonts](https://github.com/idispatch/raster-fonts). The gradient palettes
come from [cpt-city](http://seaviewsensing.com/pub/cpt-city).

The Python side of the component is independent work and contains no WLED-derived
data, which keeps it compatible with ESPHome's MIT-licensed Python tree.
