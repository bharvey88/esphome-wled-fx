# Round 2 findings

Adversarial review of `wled_fx` v0.3.1 against a real WLED 16.0.1 device (Apollo
M-1, ESP32-S3, 8 MB PSRAM, 64x64 HUB75; `/json/info` reports `"maxseg":64`,
`"arch":"ESP32-S3"`, `"psram":8257975`).

Read `NOISE.md` first. Every port-against-device number below is quoted with the
device's disagreement with itself beside it, and nothing is called a defect
unless it clears that.

Evidence paths, all outside the repository:

* device run 1: `esphome-wled-fx-ref\captures\<Name>_<id>\`
* device run 2, taken for this round: `esphome-wled-fx-ref2\captures\<Name>_<id>\`
* port runs 1 to 3, taken for this round through the display front end:
  `esphome-wled-fx-port1\captures\`, `-port2\`, `-port3\`
* pooled comparison, 2 device runs against 3 port runs:
  `esphome-wled-fx-compare2\REPORT.md`
* 30 s captures, the PS Starburst size sweep and the palette probe: session
  scratchpad folders `dev30`, `port30`, `devSBsmall`, `portSBsmall`, `devSBbig`,
  `portSBbig`, `devPal0`, `devPalDecl`
* 1D simulator sweeps at 60 and 300 pixels: WSL `/root/wfx/anim60` and
  `/root/wfx/anim300`, from
  `wled_fx_sim --size 60x1 --anim ... --frames 300 --single-pass`

Seven real defects. Two are the round 1 gamma fix landing only half way, one is
a control the whole comparison was blind to, one is a set of platform branches
that cannot be reached, and three are smaller.

---

## R2-1 (S2, front end) The engine now pre-compensates for an output gamma that neither front end applies by default, so every effect that calls `gamma8inv()` is wrong on the panel, in the opposite direction to before

**Effects:** the 18 in-effect gamma call sites, that is Matrix, Twinklefox,
Twinklecat, Noise Pal, Sunrise, Popcorn, DJ Light, Lightning, Pride 2015 and
five FFT effects, plus every particle effect through the renderer's
gamma/inverse-gamma pair.

**What WLED does.** WLED uses gamma in two places and the pair is designed to
cancel.

* Inside effect and particle code, to pre-compensate. `refs/WLED/wled00/FX.cpp:5732`:
  `    spawnColor = RGBW32(gamma8inv(175), gamma8inv(255), gamma8inv(175), 0); // use gamma inversion to restor original pre 16.0 looks`
* At the output, in `show()`. `refs/WLED/wled00/FX_fcn.cpp:1723-1724`:
  `    if (c > 0 && useGammaCorrection)`
  `      c = gamma32(c); // apply gamma correction if enabled note: applying gamma after brightness has too much color loss`
  gated at `refs/WLED/wled00/FX_fcn.cpp:1713`:
  `  bool useGammaCorrection = gammaCorrectCol && !(realtimeMode && arlsDisableGammaCorrection && !realtimeOverride);`
  with `refs/WLED/wled00/wled.h:412` `WLED_GLOBAL bool gammaCorrectCol _INIT(true);`
  and `refs/WLED/wled00/wled.h:414` `WLED_GLOBAL float gammaCorrectVal _INIT(2.2f);`.

The bus layer adds nothing. `refs/WLED/wled00/bus_manager.cpp` contains no gamma
at all and `refs/WLED/wled00/bus_wrapper.h:138` reads
`// In the following NeoGammaNullMethod can be replaced with NeoGammaWLEDMethod to perform Gamma correction implicitly`,
so the buses use the null method; `BusHub75Matrix::setPixelColor`
(`bus_manager.cpp:1109-1137`) hands R, G and B straight to `drawPixelRGB888`.
Brightness is applied after gamma, by the bus
(`FX_fcn.cpp:1802` `BusManager::setBrightness(scaledBri(b));`).

So a factory WLED emits `gamma2.2(buffer)`, and the round trip
`gamma2.2(gamma8inv(175))` is exactly 175, the number the effect author wrote.

**What the port does.** Round 1's fix (`b4d7b2a`) put WLED's real tables behind
`gamma8`, `gamma8inv`, `gamma32` and `gamma32inv`
(`components/wled_fx/wf_color.h:625-637`, tables at `wf_color.cpp:19` and
`:38`) and set `constexpr bool gammaCorrectCol = true;`
(`components/wled_fx/wf_particle.cpp:35`). Both tables are byte for byte
upstream's `calcGammaTable(2.2f)` output, recomputed here from
`refs/WLED/wled00/colors.cpp:652-662` in float32 with zero mismatches over 512
entries, and all 18 upstream in-effect call sites are present in the port. That
half is right.

The output half was left at identity and no default was changed:

* `components/wled_fx/__init__.py:680`
  `cg.add(var.set_gamma(entry.get(CONF_GAMMA_CORRECT, 1.0)))` - the display
  front end's `gamma_correct` defaults to **1.0**.
* `examples/m1-hub75.yaml` and `examples/m1-hub75-audio.yaml` set neither the
  component's `gamma_correct` nor the hub75 display's, so the shipped M-1
  configuration runs identity.
* The light front end leaves ESPHome's own `gamma_correct`, default **2.8**, in
  place, and ESPHome applies it *after* brightness scaling
  (`esphome/components/light/esp_color_correction.h:33-36`:
  `uint8_t res = esp_scale8_twice(red, this->max_brightness_.red, this->local_brightness_); return this->gamma_correct_(res);`),
  where WLED gammas first and lets the bus scale afterwards.

**What a user actually sees.** Buffer value to LED driver value, at full
brightness:

| buffer | WLED, gamma 2.2 in `show()` | port display front end at its default 1.0 | port display front end at 2.2 | port light front end at ESPHome's default 2.8 |
|---:|---:|---:|---:|---:|
| 32 | 3 | 32 | 3 | 1 |
| 64 | 12 | 64 | 12 | 5 |
| 128 | 56 | 128 | 56 | 37 |
| 192 | 137 | 192 | 137 | 115 |
| 215 | 175 | 215 | 175 | 158 |

Matrix's spawn pixel, the case round 1 used to settle the gamma value:

| stage | value |
|---|---|
| engine buffer, both sides (measured on the device in round 1) | (215, 255, 215) |
| what WLED's `show()` sends to the panel | (175, 255, 175) |
| what the port's display front end sends at its default | (215, 255, 215) |
| what the port's display front end sends at `gamma_correct: 2.2` | (175, 255, 175) |
| what the port's light front end sends at ESPHome's default 2.8 | (158, 255, 158) |

**On a HUB75 panel** (the shipped M-1 configuration) the port is missing one
whole gamma 2.2 stage. Both sides then go through the panel driver's own curve,
which on ESPHome's side is CIE1931 by default: `hub75/display.py:412-413` offers
LINEAR, CIE1931 and GAMMA_2_2 and adds no `-DHUB75_GAMMA_MODE` when the key is
absent, and the library falls back at
`managed_components/esphome__esp-hub75/include/hub75_config.h:77`
`#define HUB75_GAMMA_MODE 1  // Default: CIE1931`. One curve where WLED has two.
Midtones are driven roughly 2.3 times harder than WLED's, shadows are lifted,
and the picture reads washed out and low contrast beside the device. The
effects that call `gamma8inv()` are the worst of it: Matrix's green is driven at
215 where WLED drives 175, and the particle renderer's sub-pixel weights keep an
inverse gamma that `show()` was supposed to undo, so the halos are softer and
brighter than WLED's rather than matching them. Before `b4d7b2a` the identity
tables and the identity output cancelled by accident; after it they do not, so
for these effects the fix made the panel worse while making the pre-output
buffer right.

**On a WS2812 strip through the light front end** the port applies gamma 2.8
where WLED applies 2.2, so midtones are about a third darker than WLED's (128
becomes 37 against WLED's 56). The order is reversed as well: for a buffer value
of 128 at half brightness WLED emits `gamma2.2(128) * 128/255` = 28 and the
light front end emits `gamma2.8(64)` = 5, five times darker. A user who dims an
ESPHome strip running `wled_fx` loses the picture far faster than the same dim
on a WLED device.

**The port's two front ends also disagree with each other.** The display front
end applies its master-brightness number after the gamma table
(`components/wled_fx/wled_fx.cpp:279-283`, with the comment "Master brightness
is a plain linear dim of the finished frame, so it is applied after gamma rather
than bent by it"), which is WLED's order. The light front end applies
`scale_output_` before `set_rgbw()`
(`components/wled_fx/wled_fx_light.cpp:85-90`), so ESPHome's gamma then bends
it. The same number entity means two different things depending on which front
end is in the config.

**Are the defaults right?** No. Deviation 33 says "Set `gamma_correct: 2.2` for
the factory WLED look. The defaults are left alone here, because an ESPHome
light's gamma is the user's to set." That holds for the light front end, where
2.8 is ESPHome's own documented default. It does not hold for the display front
end, whose `gamma_correct` is this component's own option with this component's
own default, and where 1.0 is not neutral: it is the one value that makes the
engine's newly correct pre-compensation actively wrong.

**Engine or front end:** front end, plus the documentation.

**Suggested fix.** Default the display front end's `gamma_correct` to 2.2, say
in the option's description that it is WLED's `show()` stage and that 1.0 is for
a display that already applies its own curve, and set it explicitly in
`examples/m1-hub75.yaml`, `m1-hub75-audio.yaml` and the hardware test configs
with a note about the hub75 driver's own CIE1931. Apply `scale_output_` after
gamma in the light front end or say why it cannot be. Extend deviation 33 with
the two numbers a reader needs: what a buffer value of 128 reaches the LEDs as
on each path, and the brightness ordering difference.

**How to verify.** Add a check to `wled_fx_effect_test` that the display front
end at `gamma_correct: 2.2` turns a canvas pixel of 215 into 175 and at 1.0 does
not, then capture Matrix through the snapshot harness at 2.2: the spawn pixel
has to read 175, not 215.

---

## R2-2 (S2, engine) There is no `_default_palette`, so palette "Default" renders Party colours where WLED renders the effect's own palette

**Effects:** every effect whose metadata declares `pal=`, 104 of 223, whenever
the user selects palette 0.

**What WLED does.** `refs/WLED/wled00/FX_fcn.cpp:616-619`:

    sOpt = extractModeDefaults(fx, "pal"); // always extract 'pal' to set _default_palette
    if (sOpt >= 0 && loadDefaults) setPalette(sOpt);
    if (sOpt <= 0) sOpt = 6; // partycolors if zero or not set
    _default_palette = sOpt; // _default_palette is loaded into pal0 in loadPalette() (if selected)

and `refs/WLED/wled00/FX_fcn.cpp:234`:

    if (pal == 0) pal = _default_palette; // _default_palette is set in setMode(), differs depending on effect

On WLED palette 0 is not a palette. It is "whatever this effect declared, or
Party if it declared none".

**What the port does.** `components/wled_fx/wf_palette_util.cpp:150-153`:

    switch (pal) {
      case 0:
        target = *FASTLED_PALETTES[0];  // Party, WLED's fallback default

There is no `_default_palette` anywhere in `components/`. Palette 0 is always
Party, whatever the effect declared.

**Evidence, from the device.** Three effects captured twice, once with
`--set pal=0` and once left on their declared default, at pinned colours
(scratchpad `devPal0` and `devPalDecl`):

| Effect | declares | at `pal=0` the device renders | hue distance between the two |
|---|---|---|---:|
| Fire 2012 | `pal=35` | the same picture as `pal=35`, mean brightness 137.4 against 138.8 | 0.00 |
| Glitter | `pal=11` | the same picture as `pal=11`, 224.8 against 224.8 | 0.00 |
| Flow Stripe | `pal=11` | the same picture as `pal=11`, 228.5 against 230.3 | 0.25, a moving rainbow sampled at different phases |

The noise floor for these three between the two full device runs is 0.00 to
0.01. Fire 2012 and Glitter are exact. The port at `pal=0` renders Party for all
three.

One part of the behaviour the port does get right, and the device confirms it:
`color_from_palette` returns the segment colour rather than a palette entry when
the palette is 0 (`components/wled_fx/wf_segment.cpp:820`, upstream's "palette 0
is always the same as color[mcol]"). Noise 1 at `pal=0` renders solid
(255, 160, 0), the primary colour, not palette 20, on the device as well.

**Suspected root cause.** `components/wled_fx/wf_palette_util.cpp:146-153` has
no equivalent of `Segment::_default_palette`, and
`components/wled_fx/wf_engine.cpp:16-40` never records the effect's declared
palette separately from the segment's current one.

**Engine or front end:** engine.

**Suggested fix.** Keep the effect's declared `pal=`, or 6 when it is absent or
zero, in a `default_palette_` on the segment; set it in
`apply_effect_defaults_()` whether or not the palette itself is overridden; and
remap 0 to it at the top of `load_palette()`, exactly as `FX_fcn.cpp:234` does.

**How to verify.** Select Fire 2012 with the palette entity on "Default" and
compare against the same effect on palette 35. They have to be the same picture,
as they are on the device.

---

## R2-3 (S3, engine) Selecting an effect whose metadata has no `pal=` throws the palette away; WLED leaves it alone

**Effects:** 112 of the 216 effects the device offers.

**What WLED does.** `FX_fcn.cpp:617` again: `if (sOpt >= 0 && loadDefaults) setPalette(sOpt);`.
When the metadata has no `pal=`, `extractModeDefaults` returns -1 and the
palette is not touched. Same for `si` at `FX_fcn.cpp:611`:
`sOpt = extractModeDefaults(fx, "si");  if (sOpt >= 0) soundSim  = constrain(sOpt, 0, 3);`.
`m12` is the exception upstream spells out, at `FX_fcn.cpp:610`: it is reset to
`M12_Pixels` when absent.

**What the port does.** `components/wled_fx/wf_registry.h:41-54` gives
`EffectDefaults` a base `palette{0}` and `sound_sim{0}`, and
`components/wled_fx/wf_engine.cpp:36-39` writes both out on every effect change.

**Evidence.** Comparing what `fxdef: true` actually left on the device against
what the port loads, over all 216 effects present on both sides: `sx`, `ix`,
`c1`, `c2`, `c3`, `o1`, `o2`, `o3` and `m12` agree on **every single effect**.
`pal` differs on 112, all 112 are effects with no `pal=` in the metadata, and
all 112 reproduce identically in both device runs. Examples: Ripple device 3 /
port 0, Colortwinkles device 26 / port 0, Dynamic Smooth device 54 / port 0,
PacMan device 66 / port 0, Noise Pal device 35 / port 0, Scanner device 50 /
port 0.

The port's `overrides_` mechanism (`wf_engine.cpp:21-37`) softens this: once the
user has set the palette through the select entity, `OVERRIDE_PALETTE` stops the
reset. So the visible case is the first effect change after boot and any effect
change that does not go through the setter. Combined with R2-2 it is worse than
it looks, because the value it resets to means the wrong thing.

Two rows that look like differences and are not: Scanner Dual and Dynamic Smooth
read back `o1=true` from the device where the port loads false. Both effects set
`check1` in their own body (`refs/WLED/wled00/FX.cpp:1257` `SEGMENT.check1 = true;`
and `:422` the same), and the port does too
(`components/wled_fx/wf_effects_1d_c.cpp:299`,
`components/wled_fx/wf_effects_1d_b.cpp:202`). The readback is the runtime
value, not the loaded default.

**Engine or front end:** engine.

**Suggested fix.** Make `EffectDefaults` carry "not specified" for `pal` and
`si` rather than 0 and leave the segment's value alone when the metadata does
not name them, which is what `extractModeDefaults`'s -1 means. Add
`constrain(0, 7)` on `m12` and `constrain(0, 3)` on `si` while in there:
upstream has both and the port has neither.

**How to verify.** Select an effect with a `pal=`, then one without, and read the
palette entity. It has to still show the first effect's palette.

---

## R2-4 (S2, engine) The ESP32-S2 branch of the restored data budget can never be taken, the branches are in the wrong order, and PSRAM detection now decides the star count

**What WLED does.** `refs/WLED/wled00/FX.h:82-101`:

    #ifdef ESP8266
      #define MAX_NUM_SEGMENTS  16
      #define MAX_SEGMENT_DATA  (6*1024) // 6k by default
    #elif defined(CONFIG_IDF_TARGET_ESP32S2)
      #define MAX_NUM_SEGMENTS  32
      #define MAX_SEGMENT_DATA  (20*1024) // 20k by default (S2 is short on free RAM), limit does not apply if PSRAM is available
    #else
      #ifdef BOARD_HAS_PSRAM
        #define MAX_NUM_SEGMENTS  64
      #else
        #define MAX_NUM_SEGMENTS  32
      #endif
      #define MAX_SEGMENT_DATA  (64*1024) // 64k by default, limit does not apply if PSRAM is available
    #endif
    ...
    #define FAIR_DATA_PER_SEG (MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS)

The nesting matters: the S2 test comes first and the PSRAM test lives inside the
`#else`, so an S2 gets 32 and 20 KB whether or not it has PSRAM.

**What the port does.** `components/wled_fx/wf_segment.h:78-90` tests
`#if WLED_FX_PSRAM` first and `#elif defined(CONFIG_IDF_TARGET_ESP32S2)` second.
Three problems.

1. **The S2 branch is unreachable in an ESPHome build.**
   `CONFIG_IDF_TARGET_ESP32S2` lives in `sdkconfig.h`, which ESP-IDF does not
   force-include and which `wf_platform.h` never reaches: it includes only
   `esphome/core/defines.h`, which includes only `esphome/core/macros.h`. The
   generated `examples/.esphome/build/wled-fx-m1/build/compile_commands.json`
   entry for `wf_segment.cpp` has no `-include` and no `-DCONFIG_*` at all.
   ESPHome's own spelling is `USE_ESP32_VARIANT_ESP32S2`, which is a real `-D`.
   A no-PSRAM S2 therefore falls into the `#else` and gets `FAIR_DATA_PER_SEG`
   2048 and a budget of 8192 where upstream gives 640 and 2560, on the one ESP32
   variant upstream singles out as short of RAM. Fireworks Starburst renders 136
   stars where an upstream S2 renders 42.
2. **Even with the symbol fixed the order is wrong.** An S2 with PSRAM takes the
   port's first branch and gets 64 and 64 KB where upstream gives 32 and 20 KB.
3. **There is no ESP8266 branch**, so an ESP8266 would get 2048 and 8192 against
   upstream's 384 and 1536, and a cap of 65536 against 6144, while
   `components/wled_fx/wf_effects_1d_e.cpp:655` and
   `components/wled_fx/wf_effects_1d2d.cpp:234` do branch on `#ifdef ESP8266` to
   shrink `STARBURST_MAX_FRAG`. Nothing in `components/wled_fx/__init__.py`
   restricts the component to ESP32, so the two halves of the ESP8266 story
   disagree with each other.

**What is correct, and it matters.** The PSRAM detection itself works.
`WLED_FX_PSRAM` keys on `USE_PSRAM` (`components/wled_fx/wf_platform.h:23`),
which ESPHome emits unconditionally from the `psram:` component
(`esphome/components/psram/__init__.py:260` `cg.add_define("USE_PSRAM")`), and
it is present in the generated
`examples/.esphome/build/wled-fx-m1/src/esphome/core/defines.h` and absent in
`wled-fx-strip`. So the shipped M-1 configuration does get the reference
device's 64 / 64 KB / 1024 / 4096, and Fireworks Starburst really does allocate
68 stars: `wled_fx_effect_test` prints `FAIR_DATA_PER_SEG 1024, budget 4096 bytes`
and `Fireworks Starburst: 4080 bytes, 68 stars`. Round 1's fix is right for the
board it was checked on, and the device agrees with it: mean brightness 13.5 and
14.6 over two device runs against the port's 11.9, 12.5 and 11.7.

**The exposure the fix opened.** `USE_PSRAM` means "the user wrote `psram:`",
not "the board has PSRAM". Upstream's `BOARD_HAS_PSRAM` comes from the board
definition and tracks the hardware. So an ESP32-S3 with PSRAM whose YAML omits
the `psram:` block now compiles to the no-PSRAM profile, a budget of 8192, and
Fireworks Starburst at **136** stars against the reference device's 68, with
Fireworks 1D at 409 sparks against 204. Before `0d54ab6` every build got 34 and
was uniformly wrong; now the answer depends on a YAML block that has nothing to
do with effects. `psram:` also defaults to `ignore_not_found: true`, so a board
that boots without finding PSRAM still gets the uncapped profile and a 64 KB
scratch request that nothing will refuse.

**Allocation failure handling, checked and clean.** `Segment::allocate_data()`
(`components/wled_fx/wf_segment.cpp:90-135`) enforces the cap on exactly the
builds upstream enforces it on and returns false rather than dereferencing.
All 54 `allocate_data(` call sites in `wf_effects_*.cpp` check the return and
fall back through `FX_FALLBACK_STATIC`, which touches only `seg.fill`
(`components/wled_fx/wf_effects.h:33-38`); all 32 `initParticleSystem1D/2D` call
sites check, and the non-first-call path is followed by an explicit
`if (PartSys == nullptr) FX_FALLBACK_STATIC;`. The particle retry loops
terminate. The port is stricter than upstream in `candle`
(`components/wled_fx/wf_effects_1d_e.cpp:541-549`), where upstream falls through
onto a possibly null `SEGENV.data`. No budget-derived allocation is indexed by a
separately computed count. Two smaller divergences: the port's cap check runs
before the reuse short-circuit where upstream's runs after
(`wf_segment.cpp:103` against `refs/WLED/wled00/FX_fcn.cpp:164-171`), which
cannot fire while the cap is a compile-time constant; and hitting the cap is
silent where upstream sets `errorFlag = ERR_NORAM` and logs
(`FX_fcn.cpp:167-168`).

**Engine or front end:** engine.

**Suggested fix.** Reorder to match upstream (8266, then S2, then PSRAM inside
the `#else`), spell the S2 test `USE_ESP32_VARIANT_ESP32S2`, add the ESP8266
branch or say in the config validation that ESP8266 is unsupported and drop the
`#ifdef ESP8266` effect bodies, and log which profile the build took at setup so
a user with a PSRAM board and no `psram:` block can see it.

**How to verify.** Compile `examples/m1-hub75.yaml` with the `psram:` block
removed. Fireworks Starburst has to change from 68 stars to 136, which is the
whole finding in one command.

---

## R2-5 (S3, engine) The simulated beat removed a random draw, so every audio effect runs on a different random sequence from upstream's

**What WLED does.** `refs/WLED/wled00/util.cpp:662`, outside the switch, on
every call:

    samplePeak    = hw_random8() > 250;

**What the port does.** `components/wled_fx/wf_audio.cpp:98-100`:

    const uint32_t beat = ms / SIM_BEAT_PERIOD_MS;
    g_sim.sample_peak = (beat != g_sim_beat) ? 1 : 0;
    g_sim_beat = beat;

Everything the brief asks about the beat checks out. It sits outside the switch,
so it is identical in all four simulation modes, which is where upstream's line
is. The rate is measured, not inferred: `wfx.ps1 effect` prints
`si=0: 2.0 peaks per second` through `si=3`, and the same rate holds at a 23 ms
frame and at a 50 ms one. It disturbs neither the volume nor the FFT simulation:
the mode body runs first and the beat line reads only `ms`. All four mode bodies
are statement for statement upstream's (`refs/WLED/wled00/util.cpp:610-660`).
The three peak effects look sensible: Ripple Peak at 1.44 against the device's
1.28, Waterfall at 86.4, Puddlepeak at 0.54 against the device's 2.46, which is
still about five times quiet but three orders better than the 0.01 it was.

What is not recorded is that the port draws **one fewer random number per frame**
than upstream. On the host the generator is a deterministic xorshift32
(`components/wled_fx/wf_platform.cpp:12-22`), so from the first frame an audio
effect runs, every later draw in that frame and in every later frame is offset by
one from upstream's sequence, and any effect that draws randoms is on a different
sequence.

**Evidence that it already bit something.** `tools/sim/main.cpp:193-202`, on the
black allowance for Fw Starburst audio: "It sat on the winning side of that toss
until the simulated sound stopped drawing a random number per frame and moved
the shared sequence along." The allowance was widened from 2 pixels to 3 as a
result. PORTING.md deviation 27 does not mention it.

**A second, latent one in the same function.** `simulate_sound()` is memoised on
`(now, simulation_id)` (`wf_audio.cpp:31-34`) where upstream regenerates on every
call. With one segment the two agree. With a second segment only the first
caller in a frame would see the beat, and the second would read back whatever
the first wrote into `bin_num` and `max_vol` instead of the 31 and 8 the comment
at `wf_audio.cpp:103-106` promises.

**Engine or front end:** engine.

**Suggested fix.** Draw and discard one `hw_random8()` where upstream draws it
so the shared sequence stays in step, and keep the beat as the thing that sets
the flag. Record in deviation 27 that the flag is the port's and the draw is
upstream's.

**How to verify.** The Fw Starburst audio black allowance in `tools/sim/main.cpp`
should go back to 2 pixels.

---

## R2-6 (S3, engine, latent) The 1D collision binning is clamped where upstream is not

`ParticleSystem1D::handleCollisions()` in `components/wled_fx/wf_particle.cpp`
adds two lines upstream does not have:

    uint32_t maxBinParticles = std::max<uint32_t>(50, (usedParticles + 1) / 4);
    if (maxBinParticles > binArrayEntries)
      maxBinParticles = binArrayEntries;  // the bin array is sized for numParticles, this is a safety net

Upstream has only the first line (`refs/WLED/wled00/FXparticleSystem.cpp`,
`ParticleSystem1D::handleCollisions`, "do not bin small amounts, limit max to
1/4 of particles"), because its `binIndices` is a stack VLA sized from
`usedParticles` and cannot be too small. Here the array is part of the particle
system's own allocation (deviation 15) and holds a quarter of `numParticles`, so
when the particle count is under about 200 the `max(50, ...)` asks for more
slots than exist and the clamp fires, changing which particles are tested
against each other. That is a visible difference in the collide-enabled 1D
effects on a short strip. It cannot fire on a 64x64 canvas, where
`binArrayEntries` is 409, which is why no capture has seen it.

**Suggested fix.** Size the bin array from `max(50, numParticles / 4)` rather
than `numParticles / 4` and drop the clamp. Record it either way.

**How to verify.** Run PS Pinball and PS Springy with collisions on at 32 pixels
before and after.

---

## R2-7 (S3, documentation) Three of the seven new deviations understate their consequence, and one completeness claim is false

Deviations 27 to 33 were checked line by line against both sides. Four are
accurate. Three are not.

1. **Deviation 27 overstates what it is fixing.** It says "Puddlepeak, Ripple
   Peak and Waterfall draw nothing except on that flag, so with upstream's line
   all three are to all intents blank." Waterfall is not peak-gated at all: the
   `else` branch at `components/wled_fx/wf_effects_audio_fft.cpp:259-261`
   (upstream `refs/WLED/wled00/FX.cpp:7524-7526`) paints a column on every
   `secondHand` tick whatever the flag is, and with the simulation's
   `my_magnitude` of 1250 that is a 61 percent palette blend, not darkness; the
   flag only chooses the colour of the newest column. Ripple Peak measured 0.39
   against a real device's 1.28 before the change, a factor of three, not blank.
   Only Puddlepeak, at 0.01 against 2.46, was blank. The deviation also omits
   R2-5 and omits that upstream's simulated peak rate is frame-rate dependent
   (about 0.85 a second at 23 ms and 0.39 at 50 ms) while the port's is a flat 2
   a second.
2. **Deviation 30's completeness claim is false.** It says the `m12` renumber
   "is the only metadata string in the port that differs from its upstream other
   than Scrolling Text's". Diffing all 223 port metadata strings against WLED
   16.0.1 and WLED-MM: 221 match byte for byte, the two named ones differ as
   described, and **six more MM strings differ by having the trailing moon glyph
   stripped from the effect name** (Fireworks audio, Fw Starburst audio, GEQ 3D,
   Paintbrush, Popcorn audio, Snow Fall, with Multi Comet audio dropping it too
   on top of the `m12` change). The removal is deliberate and a code comment at
   `components/wled_fx/wf_effects_mm.cpp:1178-1179` says so, but PORTING.md
   mentions it nowhere, so the deviation list asserts something the deviation
   list does not cover.
3. **Deviation 31 lists four dropped keys and there is a fifth change.** The
   port's Scrolling Text string also blanks the seventh slider name, "Custom
   Font" to empty (`components/wled_fx/wf_effects_2d_a.cpp:267` against
   `refs/WLED/wled00/FX.cpp:6567`). That belongs with deviation 4, but 31 exists
   to catch what 4 does not.
4. **Deviation 33** is R2-1. It also does not say that WLED's output gamma is
   conditional on `gammaCorrectCol`, that `gammaCorrectBri` is a separate stage
   at `refs/WLED/wled00/FX_fcn.cpp:1800` (`if (gammaCorrectBri) b = gamma8(b);`)
   defaulting to false, or that the port pins `gammaCorrectCol` true so a WLED
   with gamma switched off cannot be matched at all.

Smaller citation slips, all verified: deviation 28 cites
`fastled_slim.cpp:100-104` where the commented line is at `:105`; deviation 29
quotes upstream as `max(3, SEGLEN/10)` where `refs/WLED/wled00/FX.cpp:2168` is
`const uint8_t ignition = MAX(3,SEGLEN/10);` and gives no file:line, and its "a
1 or 2 pixel light" should be "a 2 pixel light", because both sides return early
at one pixel; deviation 32's "above it the MM original goes dark" is true only
for lengths 2040 to 2047 and the equivalent bands above, since `uint8_t` wraps
rather than saturating.

Accurate as written: deviation 28's mechanism and consequence; deviation 29's
"none above three pixels", which holds because `max(3, seg_len/10) <= seg_len`
for every `seg_len >= 3`; deviation 30's renumber itself (MM
`M12_sPinwheel = 7` at `refs/WLED-MM/wled00/FX.h:413`, stock `= 4` at
`refs/WLED/wled00/FX.h:412`, and the other four `mm` strings all in the 0 to 3
band the two agree on); and deviation 32's two upstream quotes.

**Suggested fix.** Rewrite deviation 27's first paragraph to name Puddlepeak
alone and add the random-draw shift and the frame-rate point; add the moon glyph
to the deviation list and narrow 30's claim; add the seventh slider to 31; fix
the three citations.

---

## R2-8 (S3, front end) The two standard slider labels are not WLED's

`components/wled_fx/wf_registry.cpp:282`
`const char *const SLIDER_LABEL_DEFAULTS[5] = {"Speed", "Intensity", "Custom 1", "Custom 2", "Custom 3"};`

WLED's UI uses the `title` attributes in `refs/WLED/wled00/data/index.htm`,
which are `Effect speed`, `Effect intensity`, `Custom 1`, `Custom 2`,
`Custom 3`, and restores them explicitly for a `!` label at
`refs/WLED/wled00/data/index.js:1646`:
`if (i<2 && slOnOff[i]==="!") text = i==0 ? "Effect speed" : "Effect intensity";`

So every effect whose metadata leaves the first two sliders at `!`, which is
most of them, publishes "Speed" and "Intensity" here where WLED shows "Effect
speed" and "Effect intensity".

Everything else in the label layer matches, checked against
`refs/WLED/wled00/data/index.js:1630-1740`: the colour slot defaults are `Fx`,
`Bg` and `Cs` (`index.js:1695-1697`) and the port's
`COLOR_LABEL_DEFAULTS` are the same; the checkbox defaults are `Check 1`,
`Check 2` and `Check 3` (`index.htm`, the three `.ochkl` labels) and the port's
`CHECK_LABEL_DEFAULTS` are the same; the palette default is `Color palette`
(`index.js:1721`) and so is the port's; an empty item hides the control on both
sides; a numeric palette group pins the palette and hides the selector on both
sides (`index.js:1722` `isNaN(paOnOff[0])` against the port's `all_digits()`);
and an effect with no metadata at all shows two sliders, three colour slots and
the palette on both sides. No metadata string is over 255 characters, the
longest being Paintbrush at 167, so upstream's `lineBuffer[256]` truncation in
`extractModeDefaults` never bites and the port's unbounded parser cannot differ
from it on that account.

One deliberate difference worth recording rather than fixing: WLED's UI
abbreviates a named colour slot to its first two characters on the button and
prints `Ab=Full name` in a legend (`index.js:1688-1693`), while the port
publishes the full name. The port's is better and nothing records it.

**Suggested fix.** Change the two strings, or record why not.

**How to verify.** `wled_fx_effect_test`'s "Control labels" section already
asserts an exact line; update it and it will hold the change.

---

## Candidates that did not survive the evidence

Recorded so round 3 does not spend time on them.

* **PS Starburst at 0.50, which round 1 left as the first thing to attack.** It
  is not a defect. `mode_particleStarburst`
  (`components/wled_fx/wf_effects_particle_1d.cpp:986-1036` against
  `refs/WLED/wled00/FX.cpp:10297-10347`) is statement for statement upstream's,
  and so are `sprayEmit`, `particleMoveUpdate`, `renderParticle`,
  `renderLargeParticle`, `calculateNumberOfParticles1D`,
  `calculateNumberOfSources1D`, `initParticleSystem1D` and the `PSparticle1D`,
  `PSadvancedParticle1D` and `PSsource1D` layouts, all checked field by field
  including the 16 bit `minLife` and `maxLife`. `MAXPARTICLES_1D` is 2600 on
  both. The 0.50 was the capture window. The explosion interval is
  `10 + hw_random16(255 - speed)` frames, which at the default `sx=150` is 10 to
  114 frames with a mean of 62, so a 6 s window sees two to four explosions and
  the brightness is dominated by counting noise. The device's own two 6 s runs
  measure **8.2 and 2.8**, a factor of 2.9 apart, and the port's three measure
  3.6, 5.5 and 5.7, inside that range. At 30 s, at matched controls, with the
  particle size forced small and large:

  | Size | device, 30 s | port, 30 s | ratio | lit pixels a frame, device / port |
  |---|---:|---:|---:|---|
  | `c1=120`, the default | 4.60 | 5.40 | 1.17 | 39.2 / 39.8 |
  | `c1=0`, small | 2.15 | 2.83 | 1.32 | 24.8 / 27.6 |
  | `c1=255`, large | 9.60 | 10.08 | 1.05 | 75.1 / 76.5 |

  Lit pixels a frame is the product of the particle count and their width, and
  it is the number that would move if emission, lifetime or size were wrong. It
  agrees to within 2 percent at all three sizes. Verdict: clean.
* **The slow effects round 1 argued about from theory.** Captured for 30 s on
  both sides at matched controls: Sweep device 134.0 / port 129.3 (0.97), Wipe
  144.5 / 139.6 (0.97), Tri Wipe 98.7 / 90.8 (0.92), Slow Transition 255.0 /
  255.0, Fill Noise 255.0 / 255.0. The phase argument was right and is now
  measured.
* **Lightning.** Flagged in both rounds as "WLED animates and the port does
  not". The body is identical (`components/wled_fx/wf_effects_1d_e.cpp:349-389`
  against `refs/WLED/wled00/FX.cpp:1906-1943`). The interval between strikes is
  `refs/WLED/wled00/FX.cpp:1939`
  `SEGENV.aux0 = (hw_random8(255 - SEGMENT.speed) * 100);`, which at the default
  speed of 128 is 0 to 12.6 s with a mean of 6.3 s, longer than the capture
  window. The device's own two 6 s runs measure 1.4 and 0.8 mean brightness at
  frame changes of 1.44 and 0.83; the port's three measure 0.1, 0.4 and 0.0. At
  30 s the two sides are 0.55 and 0.32 on a frame change of 0.60 against 0.34,
  still two samples of the same Bernoulli trial. Artefact; it needs a capture of
  several minutes, not a better port.
* **Fill Noise.** Flagged as "one side moves and the other is still". Device
  158.1 and 155.5, port 156.9, 156.0 and 155.8; frame change 2.18 and 2.76
  against 2.40, 2.46 and 2.14. The same to three significant figures; the flag
  is the motion estimator. Clean.
* **PS Blobs at half the device's brightness.** Deviation 20,
  `has_real_audio()`: the two sides take different branches by design.
* **Every hue flag in the report except one.** See `VERDICTS.md`; each is at or
  below the device's disagreement with itself in `NOISE.md`.
* **Chase Flash Rnd lighting only the first 37 pixels of a 300 pixel strip.**
  The body is identical; the effect advances `aux1` by one pixel per seven
  rendered frames at 20 to 30 ms of internal delay, so 15 s of simulated time
  reaches pixel 37 and no further. Upstream walks at the same rate. Expected.
* **Multi Comet and Oscillate reaching only 39 and 50 percent of a 300 pixel
  strip in 15 s.** Same class: travel rate, not a bound.
