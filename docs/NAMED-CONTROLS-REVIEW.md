# Named controls: the YAML shape, for review

This is the `named-controls` branch asking for a decision on the schema before
anything lands on `main`. Nothing here is merged, tagged or released.

The problem it answers: at runtime the component exposes eight generic
entities, Speed, Intensity, Custom 1 to 3 and Check 1 to 3, plus a text sensor
saying what they mean for the effect that is running. That is the best a build
which switches effects can do, because WLED renames and hides its controls per
effect and an ESPHome entity's name is fixed when the firmware is built. It is
also confusing, and for the common case, one panel running one effect, it is
confusing for no reason.

So: when a configuration **pins one effect**, `controls: true` builds one
entity per control that effect actually uses, with WLED's own names, and
nothing called Custom 1 or Check 1.

Read the three examples, the entity lists and the errors, then tell me which
of the open questions at the end you want changed.

## The YAML

### Display front end

```yaml
wled_fx:
  id: fx
  display_id: matrix
  effect: Matrix
  controls: true
```

### Light front end

The option goes on the light effect, because on that front end the effect is
the thing that owns the engine, the canvas and the controls. A top-level
`wled_fx:` entry pointing back at the effect by id was the alternative; it
would have split `effect:` and `controls:` across two places in the file for
no gain.

```yaml
wled_fx:            # the registration stub, as before

light:
  - platform: esp32_rmt_led_strip
    id: strip
    # ...
    effects:
      - wled_fx:
          id: strip_fx
          name: Fire
          effect: Fire 2012
          controls: true
```

An `id:` on the entry or the effect is optional. With one, the generated C++
variables are readable, `fx_speed` and so on; without one, ESPHome names them.
It makes no difference to what Home Assistant shows, which is built from the
entity's name.

### The mapping form

```yaml
  controls:
    name_prefix: Fire
    icon: mdi:fire
    entity_category: config
    restore_value: true
    web_server:
      sorting_group_id: fx_group
    speed:
      name: Fall speed
      icon: mdi:speedometer
      restore_value: false
      web_server:
        sorting_weight: 10
    check2: false
```

`controls: true` is the same as `controls: {}`. The five common keys apply to
every entity; the same five plus `name` apply to one control and win over the
common ones; `false` leaves a control out.

`icon`, `entity_category` and `web_server` are passed through to the entity's
own ESPHome schema, which is what validates them, so `web_server` takes
whatever sorting fields the installed ESPHome supports and produces its own
error when it does not.

A control is addressed by its **slot** (`speed`, `intensity`, `custom1` to
`custom3`, `check1` to `check3`, `palette`, `color1` to `color3`) rather than
by the name WLED gives it, and the reason is in the Matrix table below: Matrix
has a slider and a colour slot both called Trail, so names cannot address every
control. See open question 1.

## What gets built, and where it comes from

From the effect's verbatim WLED metadata string:

| Metadata | Entity | Range |
|---|---|---|
| a used slider slot | `number`, slider mode | 0 to 255, or 0 to 31 for custom3 |
| a used checkmark | `switch` | |
| a used palette group | `select` | every palette name |
| a used colour slot | `light`, the existing `type: color1..3` platform | |

Nothing is created for a slot the effect leaves empty, which is exactly how
WLED's own UI hides them. Two rules on top of that:

* **A display always gets colour 1**, even for an effect that uses no colour
  slot, because on the display front end that light also carries the master
  brightness and the on/off for the whole panel. It is named after the
  effect's colour 1 label when there is one and "Panel" when there is not.
* **The light front end gets no colour 1** while `use_light_color` is true,
  which is the default, because the light's own colour already is segment
  colour 1. Set `use_light_color: false` and it comes back.

No initial value is written into the generated code. Every one of these
entities publishes what the engine holds in its own `setup()`, and the engine
has already applied the effect's metadata defaults, so the entity comes up on
the effect's WLED default with nothing to keep in step. The **Default** column
below is what that works out to.

### Where the labels come from

At codegen time, out of the C++, with no hand-maintained Python table:

* the metadata strings themselves from the `ENTRIES[]` registration tables in
  `components/wled_fx/wf_effects_*.cpp`, which `effect_index.py` already
  scanned for the effect names;
* the wording for a label written `!` from `SLIDER_LABEL_DEFAULTS`,
  `CHECK_LABEL_DEFAULTS`, `COLOR_LABEL_DEFAULTS` and `PALETTE_LABEL_DEFAULT`
  in `wf_registry.cpp`, which is what the firmware's own "Effect controls"
  text sensor publishes;
* the value a control falls back to when the metadata declares none from the
  member initialisers on `struct EffectDefaults` in `wf_registry.h`.

`tools/check_effect_names.py` now compares all of it against the simulator:
the strings against `--list-meta` and the parsed defaults against `--list`. If
the Python reading and the C++ reading ever part company, CI fails.

## The three examples

Entity ids below are what Home Assistant shows for a device with no
`friendly_name:` set, so the prefix is the `esphome: name:`. Adding a
`friendly_name:` changes the prefix and nothing else.

### examples/m1-matrix-named.yaml

M-1, 64x64 HUB75, pinned to Matrix.
Metadata: `Matrix@!,Spawning rate,Trail,,,Custom color;Spawn,Trail;;2`

```yaml
wled_fx:
  id: fx
  display_id: matrix
  update_interval: 23ms
  gamma_correct: 2.2
  effect: Matrix
  controls: true
```

| Slot | Entity | Name | Entity id | Default |
|---|---|---|---|---|
| speed | number | Effect speed | `number.wled_fx_m1_matrix_effect_speed` | 128 |
| intensity | number | Spawning rate | `number.wled_fx_m1_matrix_spawning_rate` | 128 |
| custom1 | number | Trail | `number.wled_fx_m1_matrix_trail` | 128 |
| check1 | switch | Custom color | `switch.wled_fx_m1_matrix_custom_color` | off |
| color1 | light | Spawn | `light.wled_fx_m1_matrix_spawn` | on, WLED's amber, also the panel master |
| color2 | light | Trail | `light.wled_fx_m1_matrix_trail` | off |

Six entities. No effect select, no palette select (Matrix uses no palette), no
Custom 2, no Custom 3, no Check 2 or 3, and no "Effect controls" text sensor,
because the answer it would give never changes.

Note `number.…_trail` and `light.…_trail`, from the slider and the colour slot
that WLED gives the same name. Different domains, so Home Assistant is happy,
and this is what WLED itself shows.

### examples/strip-fire-named.yaml

WS2812 strip, 60 pixels, pinned to Fire 2012.
Metadata: `Fire 2012@Cooling,Spark rate,,2D Blur,Boost;;!;1;pal=35,sx=64,ix=160,m12=1,c2=128`

```yaml
      - wled_fx:
          id: strip_fx
          name: Fire
          effect: Fire 2012
          update_interval: 23ms
          controls:
            restore_value: true
            custom2:
              name: Blur
```

| Slot | Entity | Name | Entity id | Default |
|---|---|---|---|---|
| speed | number | Cooling | `number.wled_fx_strip_fire_cooling` | 64 |
| intensity | number | Spark rate | `number.wled_fx_strip_fire_spark_rate` | 160 |
| custom2 | number | Blur | `number.wled_fx_strip_fire_blur` | 128 |
| custom3 | number | Boost | `number.wled_fx_strip_fire_boost` | 16, range 0 to 31 |
| palette | select | Color palette | `select.wled_fx_strip_fire_color_palette` | Fire |

Five entities, all of them remembering their value across a reboot. Fire 2012
declares no colour slots and `use_light_color` is on, so the strip's own light
entity is colour 1 and there is nothing else. WLED calls custom2 "2D Blur",
which on a strip is misleading, so the example renames it.

### examples/m1-particle-named.yaml

M-1, 64x64 HUB75, pinned to PS Fire, one of the 2D particle effects.
Metadata: `PS Fire@Speed,Intensity,Flame Height,Wind,Spread,Smooth,Cylinder,Turbulence;;!;2;pal=35,sx=110,c1=110,c2=50,c3=31,o1=1`

```yaml
  controls:
    name_prefix: Fire
    speed:
      restore_value: true
      icon: mdi:speedometer
    intensity:
      restore_value: true
    check2: false
    check1:
      entity_category: config
    check3:
      entity_category: config
    palette:
      name: Palette
```

| Slot | Entity | Name | Entity id | Default |
|---|---|---|---|---|
| speed | number | Fire Speed | `number.wled_fx_m1_particle_fire_speed` | 110, remembered |
| intensity | number | Fire Intensity | `number.wled_fx_m1_particle_fire_intensity` | 128, remembered |
| custom1 | number | Fire Flame Height | `number.wled_fx_m1_particle_fire_flame_height` | 110 |
| custom2 | number | Fire Wind | `number.wled_fx_m1_particle_fire_wind` | 50 |
| custom3 | number | Fire Spread | `number.wled_fx_m1_particle_fire_spread` | 31, range 0 to 31 |
| check1 | switch | Fire Smooth | `switch.wled_fx_m1_particle_fire_smooth` | on, diagnostic category |
| check3 | switch | Fire Turbulence | `switch.wled_fx_m1_particle_fire_turbulence` | off, diagnostic category |
| palette | select | Fire Palette | `select.wled_fx_m1_particle_fire_palette` | Fire |
| color1 | light | Fire Panel | `light.wled_fx_m1_particle_fire_panel` | on, the panel master |

Nine entities where the generic set would have been seventeen and a line of
text. `check2` is Cylinder, which wraps the panel left to right and is not
something a flat square panel wants, so it is left out. PS Fire uses no colour
slot, so colour 1 exists purely as the panel's brightness and on/off.

## The errors

Each of these is `esphome config` refusing, with the whole message reproduced.

**`controls:` with no pinned effect**

```
'controls' builds one entity per control of one effect, so the entry it is on
needs exactly one 'effect:'. Name the effect, or drop 'controls' and use the
generic 'number', 'switch' and 'select' platforms, which work whatever effect
is running.
```

**`controls:` beside a generic platform for the same controller**

```
'controls' on the wled_fx entry 'fx' already builds a named entity for every
control "Matrix" uses, and this 'number' entry adds a generic one for the same
controller. Pick one: named controls for a single pinned effect, or the
generic 'number', 'switch' and 'select' platforms with the 'controls' text
sensor, which names them for whichever effect is running. Remove this 'number'
entry, or remove 'controls'.
```

The same message covers `switch:`, `select:` and `light:`.

**`controls:` beside an allow-list that is not the pinned effect**

```
'controls' names the entities after 'Matrix', and this 'effects' allow-list
names 'Matrix', 'Metaballs' and 'PS Fire'. The named entities would be the
wrong ones for anything else that got selected. Either cut the allow-list down
to 'Matrix', which is what leaving it out does on its own, or drop 'controls'
and use the generic 'number', 'switch' and 'select' platforms with the
'controls' text sensor, which names them for whichever effect is running.
```

An allow-list naming exactly the pinned effects is accepted, so two pinned
front ends and `effects: [Matrix, Metaballs]` is fine.

**An override naming a control the effect does not use**

```
"Matrix" does not use 'custom2', so there is no entity to configure. What it
does use: speed (Effect speed), intensity (Spawning rate), custom1 (Trail),
check1 (Custom color), color1 (Spawn), color2 (Trail).
```

**`restore_value` on a colour slot**

```
'restore_value' does not apply to a colour slot. An ESPHome light restores its
own state, so a colour comes back as it was left whatever this says.
```

**`controls:` on a `wled_fx` entry that drives nothing**

The existing message for that case, unchanged, listing `controls` among the
keys that only mean something on an entry with a `display_id`.

## Flash and RAM

Three builds of the same M-1 configuration, ESP32-S3 with esp-idf, measured
with the ESPHome 2026.8.2 in `C:\Users\bharv\esphome-venv`:

| Build | Flash | RAM |
|---|---:|---:|
| Pinned to Matrix, named controls, Matrix compiled in | 828,759 | 110,163 |
| Pinned to Matrix, the generic entity set, Matrix compiled in | 836,771 | 110,947 |
| Pinned to Matrix, the generic entity set, all 64 matrix effects | 898,459 | 111,043 |

* The six named entities are **8,012 bytes of flash and 784 bytes of RAM**
  less than the seventeen generic ones they replace, which is mostly just
  eleven fewer entities.
* Compiling Matrix alone rather than the 64 a 64x64 panel offers is another
  **61,688 bytes of flash** and 96 bytes of RAM. That saving comes free with
  pinning: when every output in the configuration is pinned with `controls:`
  and no `effects:` list is written, nothing else could ever be selected, so
  the allow-list defaults to the pinned effects.
* **69,700 bytes**, about 68 KB, between the two ends.

A build that does not use `controls:` is unchanged to the byte: the platforms
it pulls in are pulled in only when it is used, and ESPHome only emits
`USE_NUMBER` and its neighbours once an entity of that kind exists.

## Verification

* `esphome config` and `esphome compile` pass for the three new examples, the
  four existing examples and the four hardware-test configurations, from the
  venv above, with the configs staged at a short path and the component path
  pointed at this worktree.
* `pytest tests` passes: 28 tests over the metadata parsing and 19 over the
  schema, the entities it builds and the errors above.
* `tools/check_effect_names.py`, against a simulator built in WSL, reports
  "name scanner agrees with the registry: 223 effects, 72 palettes" with the
  two new comparisons in it.
* No hardware. Nothing here changes a rendered frame: the named entities are
  the same C++ classes the generic platforms build, driving the same engine
  setters, and they subscribe to the controller's existing state-change
  callback rather than polling.

## How it is put together, in case the shape matters to the decision

Two pieces of machinery are worth knowing about before signing off on the
YAML, because a different YAML shape might not be able to use them.

**ESPHome loads a platform's sources only for a configuration that uses it.**
`controls:` creates entities out of nowhere, so it has to ask for the
platforms it needs, and it does that with a dynamic `AUTO_LOAD(config)`, which
ESPHome runs after every component's schema validation. ESPHome loads a
platform by adding an entry to that domain carrying nothing but
`platform: wled_fx`, so each of the four platform packages recognises that
stub and skips it. The stub shows up in `esphome config` output as a bare
`- platform: wled_fx`, which is a little odd to read and creates nothing.

**The light front end is validated before that runs**, so a light effect's
`controls:` leaves the platforms it needs in `CORE.data` and `AUTO_LOAD` picks
them up from there. That is the one piece of this that depends on ESPHome's
validation ordering rather than on a documented interface.

The four entity class declarations moved from the platform packages up into
`components/wled_fx/__init__.py`, so a generic "Custom 1" and a named
"Spawning rate" are one C++ class with one runtime.

`restore_value` is the only new C++: `WledFxNumber` and `WledFxSelect` can now
keep their value in a preference and apply it at boot as though somebody had
moved the control. Off by default, one byte of storage, nothing per frame.

## Open questions

These are the ones I would like an answer on before this merges.

1. **Addressing a control by slot.** `custom1:` and `check2:` are what you
   write to adjust or omit one, which is the same key the rest of the schema
   already uses for pinning a value. I could also accept a slug of WLED's own
   label, so `trail:` and `cylinder:`, but not instead: Matrix has a slider
   and a colour slot both called Trail, so slugs cannot address everything on
   their own, and two ways to name the same thing is its own kind of
   confusing. The error message lists every slot with WLED's name beside it,
   which I think covers the discoverability. Accept slugs as well, or leave it?

2. **`name_prefix` applies to a name you wrote too.** With `name_prefix: Fire`
   and no override, the palette is "Fire Color palette", which is why the
   particle example renames it to get "Fire Palette". The alternative is that
   an explicit `name:` is the whole name and the prefix does not touch it.
   I went with the consistent rule, but the other one may be what people
   expect.

3. **Solid and friends get two sliders that do nothing.** An effect whose
   metadata is nothing but a display name gets speed, intensity, the palette
   and all three colour slots, because that is what WLED's UI shows for one
   and what the firmware's own label code already does. For Solid, speed and
   intensity are not read by the effect. I followed the C++ rather than
   second-guessing it. Should a pinned effect with no metadata get no sliders
   instead? That would be the one place this port's answer differs from
   WLED's.

4. **`restore_value` is only on the named controls.** The C++ supports it on
   `WledFxNumber` and `WledFxSelect`, so putting it on the generic `number:`
   and `select:` platforms as well is three lines each, and it would close the
   "the control entities do not restore across a reboot" limitation in the
   README for everyone. I left it out because it was not asked for. Want it?

5. **Scrolling Text has no text entity.** Pin Scrolling Text with `controls:`
   and you get its sliders, its checkmark, its colours and its palette, but
   the string it scrolls is still only `text:` in YAML and the
   `wled_fx.set_text` action. A `text` platform entity is the obvious missing
   piece and is a separate change; say if you want it in this one.

6. **The master colour on a display.** Colour 1 there is the effect's colour
   slot, the panel's master brightness and the panel's on/off, all one light
   entity. For Matrix it is called Spawn, which is the effect's word for it
   and does not say "this also dims the panel". The alternatives are naming it
   something like "Spawn / Panel", or always calling it Panel and losing
   WLED's name, or a second brightness entity, which is worse. Left as WLED's
   name.

7. **Multi-effect configurations are out of scope**, as agreed: they keep the
   generic entities and the labels text sensor. Nothing here builds a prefixed
   entity set per effect, and the errors say so rather than half doing it.
