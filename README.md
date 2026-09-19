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
