# WLED effect engine inventory (recon for an ESPHome C++ port)

Recon only. Nothing has been ported. Everything below was read out of a local checkout, not from memory.

- Source: `https://github.com/wled/WLED`
- Tag used: **`v16.0.1`** (verified present via `git ls-remote --tags`; commit `29b389df1c1aaec6ff53aea742d17063b985906c`)
- Local shallow clone: `C:\Users\bharv\development\esphome-wled-fx\refs\WLED`
- Report generated: 2026-09-19

Tag naming note: 16.x releases use a bare `v16.x.y` scheme (`v16.0.0-beta`, `v16.0.0-rc1`, `v16.0.0`, `v16.0.1`). There is no `v0.16.0.1`. The 0.15.x line is the last to use the `v0.x` form.

---

## 1. Headline numbers

| Metric | Value |
|---|---|
| Effect slots (`MODE_COUNT`) | 220 (IDs 0 to 219) |
| Effects actually registered | **216** |
| Reserved / unused slots | 4 (IDs 142, 169, 170, 171) |
| Lines inside effect functions (`mode_*`) | **8,143** |
| `wled00/FX.cpp` total | 11,224 lines |
| `wled00/FX.h` | 1,060 lines |
| `wled00/FX_fcn.cpp` | 2,166 lines |
| `wled00/FX_2Dfcn.cpp` | 588 lines |
| `wled00/FXparticleSystem.cpp` | 1,945 lines |
| `wled00/FXparticleSystem.h` | 420 lines |
| `wled00/palettes.cpp` | ~870 lines (59 gradient palettes + 7 FastLED palettes) |
| `wled00/colors.cpp` / `colors.h` | color math and palette lookup |
| `wled00/src/dependencies/fastled_slim/` | 1,060 lines (vendored FastLED subset) |
| `usermods/audioreactive/audio_reactive.cpp` | 2,324 lines |
| Core surface to port (FX.cpp + FX.h + FX_fcn + FX_2Dfcn + particle system) | **17,403 lines** |

### Effects per category

Category is assigned with this precedence: particle 2D, particle 1D, audio FFT, audio volume, then dimensionality. Eight effects are both particle and audio, so a straight sum of "particle" and "audio" double counts them.

| Category | Count | LOC in those effect functions |
|---|---:|---:|
| 1D (includes 0D+1D on/off capable) | 115 | 2,816 |
| 2D only | 32 | 1,477 |
| 1D+2D (native both) | 9 | 522 |
| particle 2D | 16 | 1,256 |
| particle 1D | 15 | 1,327 |
| audio-reactive FFT (non-particle) | 13 | 433 |
| audio-reactive volume (non-particle) | 16 | 312 |
| **Total** | **216** | **8,143** |

Cross-cutting counts (overlapping):

| Property | Count |
|---|---:|
| Any audio reactive (`v` or `f` flag) | 37 |
| Any particle system user | 31 |
| Both particle and audio | 8 (IDs 197, 198, 199, 201, 212, 214, 215, 216) |
| Declares 2D support (`2` in flags) | 62 |
| Declares 0D support (`0` in flags, PWM / single colour) | 22 |
| Uses a palette (`color_from_palette` or `ColorFromPalette`) | 107 |
| Calls `SEGMENT.allocateData()` | 41 |

Largest single effects by LOC: Scrolling Text (185), PacMan (169), PS Springy (162), Halloween Eyes (152), Game Of Life (150), PS Fireworks (133), Fireworks 1D (128), PS Hourglass (116), Dancing Shadows (113), PS Fireworks 1D (113). The full per-effect LOC column is in the table in section 6.

---

## 2. Effect metadata string format

Every registered effect ships a `static const char _data_FX_MODE_X[] PROGMEM` string. Layout, semicolon separated:

```
Name@slider0,slider1,slider2,slider3,slider4,check1,check2,check3;color0,color1,color2;palette;flags;defaults
```

- **Sliders** map positionally to `SEGMENT.speed`, `intensity`, `custom1`, `custom2`, `custom3`, then `check1`, `check2`, `check3`. `!` means "use the default label" (FX Speed, FX Intensity, FX Custom 1/2/3). An empty entry hides that control. A `=N` suffix inside a slider name is a default value.
- **Color slots** name up to three color pickers; `!` is the default label, empty hides the slot.
- **Palette** section: `!` shows the palette picker; a leading digit is a default palette id.
- **Flags** (4th section) is a character set, parsed in `wled00/data/index.js` around line 958:
  - `0` = 0D capable (PWM / on-off strips)
  - `1` = 1D
  - `2` = 2D
  - `v` = volume reactive
  - `f` = frequency (FFT) reactive
  - A missing or empty 4th section defaults to `1` (1D only).
- **Defaults** (last section) is a `key=value` list read by `extractModeDefaults()` in `wled00/util.cpp` (line 458). Keys are segment fields such as `sx` (speed), `ix` (intensity), `c1`/`c2`/`c3`, `o1`/`o2`/`o3` (checkmarks), `pal`, `m12` (1D-to-2D mapping), `si` (sound sim), `mp12`, `rev`, `mi`.
- `extractModeDefaults()` always reads the **last** `;` group, not group index 4. That matters for malformed strings (see section 7).

Parsers to mirror: `extractModeName()`, `extractModeSlider()`, `extractModeDefaults()` in `wled00/util.cpp` lines 340 to 480.

---

## 3. Engine surface that effects depend on

### 3.1 Access macros (`wled00/FX.h` lines 105 to 112)

| Macro | Expands to | Notes |
|---|---|---|
| `SEGMENT` | `(*strip._currentSegment)` | the segment currently being rendered |
| `SEGENV` | `(*strip._currentSegment)` | identical to `SEGMENT` since 0.15; both still used in FX.cpp |
| `SEGCOLOR(x)` | `Segment::getCurrentColor(x)` | static, transition-blended colors |
| `SEGPALETTE` | `Segment::getCurrentPalette()` | static, transition-blended palette |
| `SEGLEN` | `Segment::vLength()` | static cached virtual length |
| `SEG_W` / `SEG_H` | `Segment::vWidth()` / `vHeight()` | static cached virtual 2D size |
| `PALETTE_SOLID_WRAP` | `(paletteBlend == 1 \|\| paletteBlend == 3)` | global `paletteBlend` setting |
| `PALETTE_MOVING_WRAP` | depends on `SEGMENT.speed` | ditto |
| `indexToVStrip(i, n)` | `((i) \| (int((n)+1)<<16))` | virtual-strip encoding for 1D-on-2D bar mapping |

`SEGLEN`, `SEG_W`, `SEG_H`, `SEGCOLOR` and `SEGPALETTE` are **static** class members, precomputed once per effect call in `Segment::beginDraw()`. That is a meaningful design point for a port: the hot loop reads statics, not per-instance fields.

### 3.2 Segment fields used by effects

Declared in `class Segment`, `wled00/FX.h` lines 419 to 470. The class is documented as 76 bytes.

User controls (persisted, settable from UI/JSON):
- `uint8_t speed, intensity`
- `uint8_t custom1, custom2`
- `uint8_t custom3 : 5` (0 to 31, reduced range)
- `bool check1 : 1, check2 : 1, check3 : 1`
- `uint8_t palette`, `uint32_t colors[3]`, `uint8_t mode`, `uint8_t opacity`, `uint8_t cct`, `uint8_t blendMode`
- geometry: `start, stop, startY, stopY, offset, grouping, spacing`
- option bits: `reverse, mirror, reverse_y, mirror_y, transpose, freeze, reset, on, selected`, `map1D2D : 3`, `soundSim : 2`, `set : 2`

Runtime scratch (mutable, zeroed on reset):
- `uint32_t step` (general purpose counter, 32 bit)
- `uint32_t call` (frame counter, incremented by `WS2812FX::service()` after each effect call)
- `uint16_t aux0, aux1`
- `byte *data` plus `unsigned _dataLen`

Usage frequency across the 216 effect bodies: `speed` 158, `intensity` 141, `call` 83, `data` 72, `aux0` 71, `step` 61, `custom1` 60, `check1` 59, `check2` 54, `custom2` 48, `custom3` 47, `aux1` 44, `check3` 40.

### 3.3 Effect data allocation

```cpp
bool Segment::allocateData(size_t len);   // FX_fcn.cpp:149, heap, zero-filled, no-op if already this size
void Segment::deallocateData();           // FX_fcn.cpp:192
inline uint16_t dataSize() const;
```

Global budget `MAX_SEGMENT_DATA` (`FX.h` lines 86 to 101): 6 KB on ESP8266, 20 KB on ESP32-S2, 64 KB on other ESP32s. The limit is bypassed when PSRAM is present. `FAIR_DATA_PER_SEG = MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS`. 41 of 216 effects call `allocateData()` directly; all 31 particle effects allocate through the particle system's own helper, which itself calls `SEGMENT.allocateData()`.

The near-universal pattern is:

```cpp
if (!SEGENV.allocateData(sizeof(MyState) * count)) return mode_static();  // or FX_FALLBACK_STATIC
MyState* s = reinterpret_cast<MyState*>(SEGENV.data);
```

`FX_FALLBACK_STATIC` is `{ mode_static(); return; }` (`FX.cpp:20`).

### 3.4 Pixel access

1D (`FX.h` lines 690 to 715, implementations in `FX_fcn.cpp`):
- `void setPixelColor(int n, uint32_t c) const` plus overloads for `(r,g,b,w)` and `CRGB`
- `uint32_t getPixelColor(int i) const`
- `void setRawPixelColor(int i, uint32_t)` / protected `setPixelColorRaw` / `getPixelColorRaw`
- `void blendPixelColor(int n, uint32_t, uint8_t blend)`
- `void addPixelColor(int n, uint32_t, bool preserveCR = true)`
- `void fadePixelColor(uint16_t n, uint8_t fade)`
- `bool isPixelClipped(int i) const`
- optional float/anti-aliased variants behind `WLED_USE_AA_PIXELS`

2D (`FX.h` lines 735 to 775, implementations in `FX_2Dfcn.cpp`):
- `setPixelColorXY(int x, int y, uint32_t)` and overloads; `getPixelColorXY(int x, int y)`
- `blendPixelColorXY`, `addPixelColorXY`, `fadePixelColorXY`, `isPixelXYClipped`
- `wu_pixel(uint32_t x, uint32_t y, CRGB c)` (Wu antialiased plot, credited in source to reddit u/sutaburosu)

There is no public `XY(x,y)` helper on Segment in 16.0; the index math is a private lambda inside `setPixelColorXYRaw`. Effects that write `XY(...)` define their own local lambda. When `WLED_DISABLE_2D` is set, every `*XY` entry point collapses to its 1D counterpart via inline shims (`FX.h` lines 777 to 810), which is a useful pattern to copy for a size-constrained ESPHome build.

### 3.5 Fade, blur, blend, move

1D (`FX_fcn.cpp`):
- `fill(uint32_t c)` line 1065
- `fade_out(uint8_t rate)` line 1075 (fades toward `SEGCOLOR(1)`)
- `fadeToSecondaryBy(uint8_t)` line 1099
- `fadeToBlackBy(uint8_t)` line 1106
- `blur(uint8_t amount, bool smear = false)` line 1116
- `clear()` inline

2D (`FX_2Dfcn.cpp`):
- `blur2D(uint8_t blur_x, uint8_t blur_y, bool smear)` line 246, with `blurRows()` / `blurCols()` inline wrappers
- `box_blur(unsigned radius, bool smear)` line 293 (declared but commented out of the public header, effects do not use it)
- `moveX(int delta, bool wrap)` line 365, `moveY` line 392, `move(dir, delta, wrap)` line 423
- `drawCircle` line 437, `fillCircle` line 493, `drawLine` line 511, `wu_pixel` line 566

Color math (`colors.h` / `colors.cpp`):
- `color_blend(c1, c2, blend8)` and `color_blend16(c1, c2, blend16)`
- `color_add(c1, c2, preserveCR)`
- `color_fade(c, amount, video)`
- `fast_color_scale(c, scale)` inline
- `adjust_color(CRGBW&, hueShift, satChange, valueChange)`
- `hsv2rgb_spectrum`, `rgb2hsv`, `hsv2rgb` (rainbow method via `CRGBW(CHSV32)`)
- `gamma8`, `gamma8inv`, `gamma32`
- `RGBW32(r,g,b,w)`, and `R(c)`, `G(c)`, `B(c)`, `W(c)` extraction macros

### 3.6 Palettes

- `Segment::color_from_palette(uint16_t i, bool mapping, bool moving, uint8_t mcol, uint8_t pbri = 255)` at `FX_fcn.cpp:1166`. This is the single most-used engine call after `setPixelColor`: 82 of 216 effects call it. It handles the "palette 0 falls back to segment color" rule.
- `Segment::color_wheel(uint8_t pos)` at `FX_fcn.cpp:1149`, used by 23 effects.
- `ColorFromPalette(const CRGBPalette16&, unsigned index, uint8_t brightness, TBlendType)` in `colors.cpp`, used directly by 30 effects.
- `Segment::loadPalette(CRGBPalette16& tgt, uint8_t pal)` at `FX_fcn.cpp:228`.
- `Segment::handleRandomPalette()` at `FX_fcn.cpp:435`, morphs the shared random palette once per `show()`.

Palette ID space (`wled00/const.h` lines 9 to 26):

| Range | Meaning | Count |
|---|---|---:|
| 0 | "Default", resolves to the effect's `_default_palette` | 1 |
| 1 | randomly generated, morphing | 1 |
| 2 to 5 | built from segment colors (primary, primary+secondary, +tertiary, distinct) | 4 |
| 6 to 12 | FastLED palettes (Party, Cloud, Lava, Ocean, Forest, Rainbow, Rainbow Bands) | 7 |
| 13 to 71 | cpt-city gradient palettes | 59 |
| 72 to 200 | user custom palettes (grow downward from 200) | up to 129 |
| 201 to 255 | usermod-registered palettes (grow downward from 255) | up to 55 |

`FIXED_PALETTE_COUNT = 6 + 7 + 59 = 72`.

Gamma note in `palettes.cpp`: cpt-city palettes are pre-gamma-corrected with (1.182, 1.0, 1.136) so they match the pre-0.16 look after the global 2.2 gamma; FastLED palettes get an inverse 2.2 applied. Any port that changes gamma handling will change palette colors.

### 3.7 FastLED dependency

WLED 16 **does not link FastLED**. It vendors a trimmed subset at `wled00/src/dependencies/fastled_slim/` (781 line header, 259 line cpp), plus its own math in `wled00/util.cpp` and `wled00/colors.cpp`.

From `fastled_slim.h`:
- types: `CRGB`, `CHSV`, `CRGBPalette16`, `TProgmemRGBPalette16`, `TBlendType` (`NOBLEND`, `LINEARBLEND`, `LINEARBLEND_NOWRAP`), gradient palette typedefs
- scaling: `scale8`, `scale8_video`, `scale16`, `qadd8`, `qsub8`, `qmul8`, `abs8`, `lerp8by8`
- waves: `triwave8`, `triwave16`, `quadwave8`, `cubicwave8`, `ease8InOutQuad`, `ease8InOutCubic`, `ease16InOutCubic`
- color: `hsv2rgb_rainbow`, `fill_solid_RGB`, `fill_gradient_RGB`, `getAverageLight`, `nblendPaletteTowardPalette`

WLED's own replacements (declared in `wled00/fcn_declare.h`, defined in `wled00/util.cpp`):
- trig: `sin8_t`, `cos8_t`, `sin16_t`, `cos16_t`, `sin_t`/`cos_t`/`tan_t` (macros for `sin_approx`/`cos_approx`/`tan_approx`), `atan2_t`, `asin_t`, `acos_t`, `atan_t<T>`, `floor_t`, `fmod_t`, `sqrt32_bw`
- beats: `beat8`, `beat16`, `beat88`, `beatsin8_t`, `beatsin16_t`, `beatsin88_t` (all take an optional `timebase`)
- noise: `perlin8` (1/2/3 arg), `perlin16` (1/2/3 arg), `perlin1D_raw`, `perlin2D_raw`, `perlin3D_raw`, `hashInt`. Legacy aliases `#define inoise8 perlin8` and `#define inoise16 perlin16` exist but WLED's own AGENTS.md says not to use them in new code.
- random: `hw_random()`, `hw_random8()`, `hw_random16()` read the ESP hardware RNG register directly (`WDEV_RND_REG` on ESP32, `RANDOM_REG32` on ESP8266), and `#define random hw_random` shadows Arduino `random()`. **This is a porting trap**: `random()` inside FX.cpp is not Arduino's PRNG, and it is not reproducible.
- a deterministic PRNG class exists at `wled00/prng.h` (LCG `seed*3001+31683`, xorshift), instantiated once in FX.cpp as `static PRNG prng(hw_random())` and used by the handful of effects that need a repeatable sequence (`setSeed` appears 6 times).
- `mapf(float, ...)` float map.

Usage counts across effect bodies: `hw_random16` 51, `hw_random8` 44, `hw_random` 21, `random8` 6, `perlin8` 20, `perlin16` 12, `sin8_t` 17, `cos8_t` 11, `sin16_t` 12, `beatsin8_t` 24, `beatsin16_t` 7, `cubicwave8` 9, `sin_t`/`cos_t` 7 each, `sqrt32_bw` 7, `scale8` 7, `qsub8` 6, `abs8` 7, `gamma8inv` 11, `CRGB` 12, `CHSV` 13, `CRGBW` 6.

### 3.8 Timing

- `WS2812FX::now` is `unsigned long` (`FX.h:957`, declared next to `timebase`). Set once per `service()` call as `millis() + timebase`, so every effect in a frame sees the same timestamp and the whole animation can be time-shifted by changing `timebase`.
- Effects read it as `strip.now` (102 of 216 effect bodies do). A handful also call `millis()` (7) or `micros()` (10) directly.
- Frame pacing lives in `WS2812FX::service()` (`FX_fcn.cpp:1318`): `_frametime`, `_targetFps`, `FPS_UNLIMITED`, `MIN_FRAME_DELAY`, `_lastServiceShow`.
- Effects must not call `delay()`.

### 3.9 How an effect is invoked

`WS2812FX::service()` (`FX_fcn.cpp:1318`), per segment, per frame:

1. `seg.handleTransition()` then `seg.resetIfRequired()`
2. skip if `!seg.isActive()` or `seg.freeze`
3. `seg.beginDraw(prog)` (`FX_fcn.cpp:407`) precomputes `_vWidth`, `_vHeight`, `_vLength`, `_currentColors[3]`, `_currentPalette` (blended if in transition)
4. `strip._currentSegment = &seg` so `SEGMENT`/`SEGENV` resolve
5. `_mode[seg.mode]()` calls the effect; signature is `typedef void (*mode_ptr)()` (`FX.h:815`) with **no return value**
6. `seg.call++`
7. if blending between two effects, repeat steps 3 to 6 for the old segment with `Segment::modeBlend(true)`
8. after all segments: `Segment::handleRandomPalette()`, then `show()`

The 0.14/0.15-era `uint16_t mode_x()` signature that returned a frame delay is **gone**. Speed is now expressed entirely through the effect's own arithmetic against `strip.now`.

### 3.10 2D mapping and 1D-on-2D

- `Segment::maxWidth`, `Segment::maxHeight` are static, set by `WS2812FX::setUpMatrix()` (`FX_2Dfcn.cpp:21`) from the panel configuration into a ledmap.
- `virtualWidth()` (`FX_fcn.cpp:658`), `virtualHeight()` (665), `virtualLength()` (697) account for grouping, spacing, transpose and the 1D-to-2D mapping mode.
- `mapping1D2D_t` (`FX.h:407`): `M12_Pixels`, `M12_pBar`, `M12_pArc`, `M12_pCorner`, `M12_sPinwheel`. Selected per segment via `map1D2D`, set from metadata key `m12`.
- Pinwheel mapping has its own helpers `getPinwheelLength()` (`FX_fcn.cpp:676`) and `setPinwheelParameters()` (680).
- `nrOfVStrips()` returns `virtualWidth()` in `M12_pBar` mode; 6 effects loop over virtual strips using `indexToVStrip()` and a local `runStrip()` lambda.
- `is2D()` is `(width() > 1 && height() > 1)`. 43 effect bodies branch on it.
- `strip.isMatrix` is read by 39 effect bodies.

### 3.11 Audio data path

Effects never talk to the audio hardware. They call a single file-local helper in `FX.cpp:121`:

```cpp
static um_data_t* getAudioData() {
  um_data_t *um_data;
  if (!UsermodManager::getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE))
    um_data = simulateSound(SEGMENT.soundSim);   // fallback when the usermod is absent
  return um_data;
}
```

`um_data_t` (`wled00/fcn_declare.h:329`) is a tagged pointer array:

```cpp
typedef struct UM_Exchange_Data {
  size_t      u_size;   // 8
  um_types_t *u_type;   // array of type tags
  void      **u_data;   // array of pointers
} um_data_t;
```

Layout as filled by the audioreactive usermod (`usermods/audioreactive/audio_reactive.cpp:1356`) and mirrored by `simulateSound()` (`wled00/util.cpp:568`):

| Index | Type | Contents | Notes |
|---:|---|---|---|
| 0 | `float` | `volumeSmth` | smoothed volume |
| 1 | `uint16_t` (`UMT_UINT16`) | `volumeRaw` | **read as `int16_t` in some effects, `float` in the FX.cpp doc comment; the doc comment is wrong** |
| 2 | `uint8_t[16]` | `fftResult` | the GEQ bins, `NUM_GEQ_CHANNELS = 16` |
| 3 | `uint8_t` | `samplePeak` | beat flag |
| 4 | `float` | `FFT_MajorPeak` | dominant frequency in Hz |
| 5 | `float` | `my_magnitude` | magnitude of that peak |
| 6 | `uint8_t` | `maxVol` | **written back by effects** from a UI slider |
| 7 | `uint8_t` | `binNum` | **written back by effects** from a UI slider |

`u_size` is 8. The FX.cpp comment block at lines 47 to 66 documents a ninth entry (`fftBin`, `u_data[8]`) that does not exist in either producer. Reading it would be out of bounds; no shipped effect does.

`simulateSound(uint8_t simulationId)` (`wled00/util.cpp:568`) synthesises the same struct from `millis()` so all 37 audio effects still run with no microphone. The simulation id comes from `SEGMENT.soundSim` (2 bits) and is set from the metadata key `si`. This is the obvious seam for an ESPHome port: implement a `um_data_t`-shaped provider and every audio effect works unchanged.

Frequency constants used by FFT effects live in FX.cpp lines 76 to 84: `MAX_FREQUENCY = 11025`, `MAX_FREQ_LOG10 = 4.04238f` (22 kHz sampling, Nyquist).

### 3.12 Shared base functions inside FX.cpp

Many effects are three-line wrappers around a shared implementation. Porting the base functions first covers a large fraction of the total. LOC measured by brace matching.

| Base function | FX.cpp line | LOC | Serves |
|---|---:|---:|---|
| `mode_gravcenter_base` | 6805 | 83 | Gravcenter, Gravcentric, Gravimeter, Gravfreq |
| `chase` | 895 | 63 | all Chase variants |
| `twinklefox_base` | 2642 | 57 | Twinklefox, Twinklecat |
| `twinklefox_one_twinkle` | 2580 | 56 | ditto |
| `soapPixels` | 7834 | 51 | Soap (2D and 1D) |
| `ripple_base` | 2492 | 50 | Ripple, Ripple Rainbow, Ripple Peak |
| `runStrip` | 2166 | 31 | virtual-strip iteration |
| `sinelon_base` | 3336 | 31 | Sinelon, Sinelon Dual, Sinelon Rainbow |
| `running_base` | 594 | 27 | Running, Running Dual, Running Random |
| `spots_base` | 2911 | 26 | Spots, Spots Fade |
| `blink` | 195 | 24 | Blink, Blink Rainbow, Strobe, Strobe Rainbow |
| `running` | 546 | 23 | Running Lights |
| `phased_base` | 4308 | 22 | Phased, Phased Noise |
| `tristate_square8` | 102 | 18 | tri-state waveform utility |
| `pacifica_one_layer` | 4165 | 14 | Pacifica |
| `getAudioData` | 120 | 9 | all 37 audio effects |
| `sin_gap` | 90 | 4 | phase-shifted sine utility |

Also file-scope in FX.cpp: `static PRNG prng(hw_random())`.

### 3.13 Call frequency ranking (across all 216 effect bodies)

Counting distinct call sites per effect (an effect that calls something twice counts once). This is the practical priority list for the port.

```
setPixelColor        94   color_from_palette   82   SEGCOLOR            73
map                  64   hw_random16          51   hw_random8          44
is2D                 43   allocateData         42   fill                41
color_blend          34   PartSys->update      34   setPixelColorXY     34
PartSys->updateSystem 34  ColorFromPalette     30   getAudioData        30
blur                 29   setMotionBlur        29   fadeToBlackBy       27
beatsin8_t           24   color_wheel          23   fade_out            23
setKillOutOfBounds   23   hw_random            21   perlin8             20
setWallHardness      20   enableParticleCollisions 19  sprayEmit        19
initParticleSystem2D 19   initParticleSystem1D 18   sin8_t              17
setGravity           16   setParticleSize      16   particleMoveUpdate  14
applyFriction        14   addPixelColorXY      14   setUsedParticles    14
getPixelColor        13   CHSV                 13   setSmearBlur        13
setBounceY           13   sin16_t              12   CRGB                12
perlin16             12   setBounceX           12   setColorByPosition  12
gamma8inv            11   cos8_t               11   setWrapX            11
```

---

## 4. Particle system

Files: `wled00/FXparticleSystem.h` (420 lines), `wled00/FXparticleSystem.cpp` (1,945 lines). Two independent classes, each removable via `WLED_DISABLE_PARTICLESYSTEM2D` / `WLED_DISABLE_PARTICLESYSTEM1D`. ESP8266 cannot build both at once (a `#error` in FX.cpp enforces this). 31 effects (IDs 187 to 217) use them, 1,256 + 1,327 = 2,583 lines of effect code on top of the 1,945 line engine.

### 4.1 Data structures

| Struct | Size | Fields |
|---|---:|---|
| `PSparticle` | 10 bytes | `int16_t x, y`, `uint16_t ttl`, `int8_t vx, vy`, `uint8_t hue, sat` |
| `PSparticleFlags` | 1 byte | `outofbounds, collide, perpetual, custom1..custom5` bitfield |
| `PSadvancedParticle` | 2 bytes | `uint8_t size, forcecounter` |
| `PSsizeControl` | 8 bytes | asymmetry, asymdir, maxsize, minsize, grow/shrink/pulsate/wobble counters and flags |
| `PSsource` | 20 bytes | `minLife, maxLife`, an embedded `PSparticle`, flags, `var`, `vx`, `vy`, `size` |
| `PSsettings2D` | 1 byte | `wrapX, wrapY, bounceX, bounceY, killoutofbounds, useGravity, useCollisions, colorByAge` |
| `PSparticle1D` | 8 bytes | `int32_t x`, `uint16_t ttl`, `int8_t vx`, `uint8_t hue` |
| `PSparticleFlags1D` | 1 byte | `outofbounds, collide, perpetual, reversegrav, forcedirection, fixed, custom1, custom2` |
| `PSadvancedParticle1D` | 3 bytes | `sat, size, forcecounter` |
| `PSsource1D` | 20 bytes | as 2D but 1D |
| `PSsettings1D` | 1 byte | `wrap, bounce, killoutofbounds, useGravity, useCollisions, colorByAge, colorByPosition` |

`ParticleSystem2D` itself is documented as ~60 bytes. Sub-pixel resolution: `PS_P_RADIUS = 64` (6-bit shift) in 2D, `PS_P_RADIUS_1D = 32` (5-bit shift) in 1D.

### 4.2 Memory model

Allocation goes through `SEGMENT.allocateData()`, so the particle system lives inside the segment data budget (section 3.3). `allocateParticleSystemMemory2D()` (`FXparticleSystem.cpp:1078`):

```
sizeof(ParticleSystem2D)
+ numparticles * (sizeof(PSparticleFlags) + sizeof(PSparticle))   // 11 bytes each
+ numparticles * sizeof(PSadvancedParticle)   if advanced         // +2
+ numparticles * sizeof(PSsizeControl)        if sizecontrol      // +8
+ numsources   * sizeof(PSsource)                                 // 20 each
+ additionalbytes                                                 // FX scratch, reached via PSdataEnd
```

Particle count defaults to one per pixel, clamped to `MAXPARTICLES_2D` (256 on ESP8266, 1024 on S2, 2048 elsewhere) and rounded up to a multiple of 4 for alignment. Advanced properties reduce the count so RAM stays flat. Size control divides the count by 8. `initParticleSystem2D()` retries with fewer particles down to a floor of 5 before failing. 1D limits: `MAXPARTICLES_1D` 320 / 1300 / 2600, `MAXSOURCES_1D` 16 / 32 / 64.

The render framebuffer is **not** owned by the system: `framebuffer = SEGMENT.getPixels()` (`FXparticleSystem.cpp:1034` and `:1786`). The one exception is 1D-mapped-to-2D rendering, which carves a local buffer out of the segment data right after the sources array (`:1781`). `MAX_MEMIDLE 10` frames governs deallocation, with a comment warning that freeing mid-effect crashes.

### 4.3 Public API used by effects

`ParticleSystem2D`:
- lifecycle: constructor `(width, height, numparticles, numsources, isadvanced, sizecontrol)`, `updateSystem()` (call first every frame), `update()` (move plus render), `updateFire(intensity)`
- emit: `sprayEmit(const PSsource&)`, `flameEmit(const PSsource&)`, `angleEmit(PSsource&, angle, speed)`
- physics: `particleMoveUpdate(...)`, `applyGravity(...)`, `applyForce(...)` (per particle, by index, or global), `applyAngleForce(...)`, `applyFriction(...)`, `pointAttractor(index, attractor, strength, swallow)`
- settings: `setUsedParticles`, `setCollisionHardness`, `setWallHardness`, `setWallRoughness`, `setMatrixSize`, `setWrapX`, `setWrapY`, `setBounceX`, `setBounceY`, `setKillOutOfBounds`, `setSaturation`, `setColorByAge`, `setMotionBlur`, `setSmearBlur`, `setParticleSize`, `setGravity`, `enableParticleCollisions`
- public data effects poke directly: `particles`, `particleFlags`, `sources`, `advPartProps`, `advPartSize`, `PSdataEnd`, `maxX`, `maxY`, `maxXpixel`, `maxYpixel`, `numSources`, `usedParticles`, `perParticleSize`

`ParticleSystem1D` is the same shape minus the 2D-only pieces, plus `setColorByPosition()`, `setSize(x)`, `setWrap`, `setBounce`.

Free functions: `initParticleSystem2D(PartSys&, requestedsources, additionalbytes, advanced, sizecontrol)`, `calculateNumberOfParticles2D`, `calculateNumberOfSources2D`, `allocateParticleSystemMemory2D`, and the four 1D equivalents. `initParticleSystem1D` additionally takes `fractionofparticles`.

Private internals a port must reimplement but effects never touch: `render()`, `renderParticle()`, `renderLargeParticle()`, `handleCollisions()`, `collideParticles()`, `fireParticleupdate()`, `updatePSpointers()`, `updateSize()`, `getParticleXYsize()`, `bounce()`, plus the inline `calculateEllipseBrightness()` helper and the file-local `calcForce_dv`, `checkBoundsAndWrap`, `fast_color_scaleAdd`.

`initParticleSystem2D()` hard-fails with `ERR_NOT_IMPL` when `!strip.isMatrix`, so a 1D-only ESPHome build simply loses the 16 particle-2D effects.

The 2D system declares `friend class ParticleSystem2D` inside `Segment`, so it reaches `Segment`'s protected raw-pixel accessors. Any port has to preserve that access or widen the interface.

---

## 5. License facts

| Item | License | Evidence |
|---|---|---|
| WLED as a whole, at `v16.0.1` | **EUPL v1.2 or later** | `LICENSE`, 293 lines, header reads `Copyright (c) 2016-present Christian Schwinne and individual WLED contributors / Licensed under the EUPL v. 1.2 or later`, followed by the full official EUPL v1.2 text |
| `wled00/FX.cpp` | EUPL v1.2 or later | header: `Harm Aldick - 2016 ... Copyright (c) 2016 Harm Aldick / Licensed under the EUPL v. 1.2 or later / Adapted from code originally licensed under the MIT license / Modified heavily for WLED` |
| `wled00/FX.h` | EUPL v1.2 or later | same Aldick header, plus `Segment class/struct (c) 2022 Blaz Kristan (@blazoncek)` |
| `wled00/FX_fcn.cpp` | EUPL v1.2 or later | same Aldick header |
| `wled00/FX_2Dfcn.cpp` | EUPL v1.2 or later | `Copyright (c) 2022 Blaz Kristan (https://blaz.at/home) / Licensed under the EUPL v. 1.2 or later / Adapted from code originally licensed under the MIT license / Parts of the code adapted from WLED Sound Reactive` |
| `wled00/FXparticleSystem.cpp` and `.h` | EUPL v1.2 or later | identical headers: `by DedeHai (Damian Schneider) 2013-2024 / Copyright (c) 2024 Damian Schneider / Licensed under the EUPL v. 1.2 or later`. No MIT-adaptation clause on these two files, so the particle system is pure EUPL. |
| `wled00/src/dependencies/fastled_slim/` | **MIT** | `LICENSE.txt`: `The MIT License (MIT) / Copyright (c) 2013 FastLED modified by @dedehai`. Header comment: `Code originally from FastLED version 3.6.0. Optimized for WLED use by @dedehai` |
| `wled00/colors.cpp` and `colors.h` | EUPL (file), with MIT-derived functions called out | in-file note: `functions in this file derived from FastLED @ 3.6.0 are marked with a comment containing "derived from FastLED" ... therefore licensed under the MIT license` |
| `wled00/palettes.cpp` | EUPL (file), FastLED palettes MIT | in-file note: `Palettes imported from FastLED @ 3.6.0 are licensed under the MIT license`. cpt-city palettes come from `http://seaviewsensing.com/pub/cpt-city` |
| `usermods/audioreactive/audio_reactive.cpp` | EUPL, no separate per-file license header | inherits the repository license |
| `wled00/prng.h` | no per-file header, inherits EUPL | |

Practical consequences for an ESPHome component:

1. **EUPL v1.2 is a strong copyleft licence with a network/distribution trigger.** Distributing a derivative (which a port of FX.cpp plainly is) requires the derivative to be released under the EUPL v1.2 or one of its Appendix-listed compatible licences (GPL-2.0, GPL-3.0, AGPL-3.0, LGPL, EPL, MPL-2.0, CeCILL, OSL, EUPL). ESPHome's own codebase is **GPL-3.0** for the Python side and **MIT** for the generated C++ / esphome-core in some historical parts, so licence compatibility needs a deliberate decision before any code is copied rather than reimplemented.
2. The vendored FastLED subset is MIT and can be reused freely with attribution. Reimplementing `sin8_t`, `beatsin*`, `perlin*`, `hw_random*`, `color_blend` from WLED's own files does **not** get you MIT terms, since those are WLED-authored EUPL code even where they were inspired by FastLED.
3. The particle system carries no MIT heritage at all. It is 100% EUPL, single author, 2,365 lines across header and cpp.
4. cpt-city gradient palette data has its own upstream provenance; WLED does not restate a licence for the palette data beyond the repository licence.

None of this is legal advice; it is what the files say.

---

## 6. Full effect table

Columns:

- **ID**: `FX_MODE_*` numeric id, the value stored in a segment and in presets.
- **Name**: the display name, the part of the metadata string before `@`.
- **Function**: the C++ `mode_*` function, all `void f()`.
- **Category**: primary bucket, precedence particle 2D, particle 1D, audio FFT, audio volume, then dimensionality.
- **Dims**: dimensionality declared in the flags field.
- **Audio**: `FFT`, `volume`, or `no`.
- **Sliders**: the `@` group verbatim, positional: speed, intensity, custom1, custom2, custom3, check1, check2, check3.
- **Color slots** / **Palette** / **Flags** / **Defaults**: the remaining metadata groups verbatim.
- **LOC**: lines from the function signature to its closing brace, measured by brace matching. Wrapper effects show a small number because the work lives in a shared base (section 3.12).
- **FX.cpp line**: 1-based line of the function signature at tag v16.0.1.
- **Segment state used**: which of `speed, intensity, custom1..3, check1..3, aux0, aux1, step, call, data` appear in the body.
- **Engine helpers used**: which engine entry points appear in the body, filtered to a curated list.

| ID | Name | Function | Category | Dims | Audio | Sliders (@ group) | Color slots | Palette | Flags | Defaults | LOC | FX.cpp line | Segment state used | Engine helpers used |
|---:|---|---|---|---|---|---|---|---|---|---|---:|---:|---|---|
| 0 | Solid | mode_static | 1D | 0D+1D | no | (none) | | | (none=1D) | | 6 | 137 | none | SEGCOLOR, fill |
| 1 | Blink | mode_blink | 1D | 0D+1D | no | !,Duty cycle | !,! | ! | 01 |  | 3 | 224 | none | SEGCOLOR |
| 2 | Breathe | mode_breath | 1D | 0D+1D | no | ! | !,! | ! | 01 |  | 15 | 432 | speed | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, sin16_t, strip.now |
| 3 | Wipe | mode_color_wipe | 1D | 1D | no | !,! | !,! | ! | (none=1D) |  | 3 | 316 | none | none |
| 4 | Wipe Random | mode_color_wipe_random | 1D | 1D | no | ! |  | ! | (none=1D) |  | 3 | 335 | none | none |
| 5 | Random Colors | mode_random_color | 1D | 0D+1D | no | !,Fade time |  | ! | 01 |  | 25 | 354 | speed,intensity,aux0,aux1,step,call | fill, color_wheel, color_blend, hw_random8, random8, strip.now |
| 6 | Sweep | mode_color_sweep | 1D | 1D | no | !,! | !,! | ! | (none=1D) |  | 3 | 325 | none | none |
| 7 | Dynamic | mode_dynamic | 1D | 1D | no | !,!,,,,Smooth |  | ! | (none=1D) |  | 28 | 386 | speed,intensity,check1,step,call,data | setPixelColor, blendPixelColor, fill, color_wheel, allocateData, SEGLEN, hw_random8, random8, strip.now |
| 8 | Colorloop | mode_rainbow | 1D | 0D+1D | no | !,Saturation |  | ! | 01 |  | 10 | 514 | speed,intensity | fill, color_wheel, color_blend, strip.now |
| 9 | Rainbow | mode_rainbow_cycle | 1D | 1D | no | !,Size |  | ! | (none=1D) |  | 10 | 530 | speed,intensity | setPixelColor, color_wheel, SEGLEN, strip.now |
| 10 | Scan | mode_scan | 1D | 1D | no | !,# of dots,,,,,Overlay | !,!,! | ! | (none=1D) |  | 3 | 496 | none | none |
| 11 | Scan Dual | mode_dual_scan | 1D | 1D | no | !,# of dots,,,,,Overlay | !,!,! | ! | (none=1D) |  | 3 | 505 | none | none |
| 12 | Fade | mode_fade | 1D | 0D+1D | no | ! | !,! | ! | 01 |  | 8 | 453 | speed | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, triwave16, strip.now |
| 13 | Theater | mode_theater_chase | 1D | 1D | no | !,Gap size | !,! | ! | (none=1D) |  | 3 | 575 | none | SEGCOLOR |
| 14 | Theater Rainbow | mode_theater_chase_rainbow | 1D | 1D | no | !,Gap size | ,! | ! | (none=1D) |  | 3 | 585 | step | color_wheel, SEGCOLOR |
| 15 | Running | mode_running_lights | 1D | 1D | no | !,Wave width | !,! | ! | (none=1D) |  | 3 | 636 | none | none |
| 16 | Saw | mode_saw | 1D | 1D | no | !,Width | !,! | ! | (none=1D) |  | 3 | 645 | none | none |
| 17 | Twinkle | mode_twinkle | 1D | 1D | no | !,! | !,! | ! | (none=1D) | m12=0 | 27 | 655 | speed,intensity,aux0,aux1,step | setPixelColor, fade_out, color_from_palette, SEGLEN, hw_random, strip.now |
| 18 | Dissolve | mode_dissolve | 1D | 1D | no | Repeat speed,Dissolve speed,,,,Random,Complete | !,! | ! | (none=1D) |  | 3 | 746 | check1 | color_wheel, SEGCOLOR, hw_random8, random8 |
| 19 | Dissolve Rnd | mode_dissolve_random | 1D | 1D | no | Repeat speed,Dissolve speed | ,! | ! | (none=1D) |  | 3 | 755 | none | color_wheel, hw_random8, random8 |
| 20 | Sparkle | mode_sparkle | 1D | 1D | no | !,,,,,,Overlay | !,! | ! | (none=1D) | m12=0 | 14 | 764 | speed,check2,aux0,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, hw_random16, random16, strip.now |
| 21 | Sparkle Dark | mode_flash_sparkle | 1D | 1D | no | !,!,,,,,Overlay | Bg,Fx | ! | (none=1D) | m12=0 | 13 | 784 | speed,intensity,check2,aux0,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now |
| 22 | Sparkle+ | mode_hyper_sparkle | 1D | 1D | no | !,!,,,,,Overlay | Bg,Fx | ! | (none=1D) | m12=0 | 16 | 804 | speed,intensity,check2,aux0,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now |
| 23 | Strobe | mode_strobe | 1D | 0D+1D | no | ! | !,! | ! | 01 |  | 3 | 242 | none | SEGCOLOR |
| 24 | Strobe Rainbow | mode_strobe_rainbow | 1D | 0D+1D | no | ! | ,! | ! | 01 |  | 3 | 251 | call | color_wheel, SEGCOLOR |
| 25 | Strobe Mega | mode_multi_strobe | 1D | 0D+1D | no | !,! | !,! | ! | 01 |  | 22 | 826 | speed,intensity,aux0,aux1,step | setPixelColor, fill, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 26 | Blink Rainbow | mode_blink_rainbow | 1D | 0D+1D | no | Frequency,Blink duration | !,! | ! | 01 |  | 3 | 233 | call | color_wheel, SEGCOLOR |
| 27 | Android | mode_android | 1D | 1D | no | !,Width | !,! | ! | (none=1D) | m12=1 | 34 | 854 | speed,intensity,aux0,aux1,step,data | setPixelColor, color_from_palette, allocateData, SEGCOLOR, SEGLEN, strip.now |
| 28 | Chase | mode_chase_color | 1D | 1D | no | !,Width | !,!,! | ! | (none=1D) |  | 3 | 963 | none | SEGCOLOR |
| 29 | Chase Random | mode_chase_random | 1D | 1D | no | !,Width | !,,! | ! | (none=1D) |  | 3 | 972 | none | SEGCOLOR |
| 30 | Chase Rainbow | mode_chase_rainbow | 1D | 1D | no | !,Width | !,! | ! | (none=1D) |  | 8 | 981 | step,call | color_wheel, SEGCOLOR, SEGLEN |
| 31 | Chase Flash | mode_chase_flash | 1D | 1D | no | ! | Bg,Fx | ! | (none=1D) |  | 32 | 1083 | speed,aux0,aux1,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 32 | Chase Flash Rnd | mode_chase_flash_random | 1D | 1D | no | ! | !,! | ! | (none=1D) |  | 37 | 1121 | speed,aux0,aux1,step,call | setPixelColor, color_wheel, SEGCOLOR, SEGLEN, strip.now |
| 33 | Rainbow Runner | mode_chase_rainbow_white | 1D | 1D | no | !,Size | Bg | ! | (none=1D) |  | 8 | 995 | step,call | color_wheel, SEGCOLOR, SEGLEN |
| 34 | Colorful | mode_colorful | 1D | 1D | no | !,Saturation | 1,2,3 | ! | (none=1D) |  | 37 | 1009 | speed,intensity,aux0,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 35 | Traffic Light | mode_traffic_light | 1D | 1D | no | !,US style | ,! | ! | (none=1D) |  | 24 | 1052 | speed,intensity,aux0,step | setPixelColor, color_from_palette, SEGLEN, strip.now |
| 36 | Sweep Random | mode_color_sweep_random | 1D | 1D | no | ! |  | ! | (none=1D) |  | 3 | 344 | none | none |
| 37 | Chase 2 | mode_running_color | 1D | 1D | no | !,Width | !,! | ! | (none=1D) |  | 3 | 1164 | none | SEGCOLOR |
| 38 | Aurora | mode_aurora | 1D | 1D | no | !,! | 1,2,3 | ! | (none=1D) | sx=24,pal=50 | 34 | 4918 | speed,intensity,aux1,data | setPixelColor, color_from_palette, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, CRGB, gamma8 |
| 39 | Stream | mode_running_random | 1D | 1D | no | !,Zone size |  | ! | (none=1D) |  | 30 | 1173 | speed,intensity,aux0,aux1,call | setPixelColor, color_wheel, SEGLEN, hw_random, strip.now |
| 40 | Scanner | mode_larson_scanner | 1D | 1D | no | !,Trail,Delay,,,Dual,Bi-delay | !,!,! | ! | (none=1D) | m12=0,c1=0 | 41 | 1209 | speed,intensity,custom1,check1,check2,aux0,aux1,step | setPixelColor, fade_out, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 41 | Lighthouse | mode_comet | 1D | 1D | no | !,Fade rate | !,! | ! | (none=1D) |  | 20 | 1265 | speed,intensity,aux0,call | setPixelColor, fade_out, color_from_palette, SEGLEN, strip.now |
| 42 | Fireworks | mode_fireworks | 1D+2D | 1D+2D | no | ,Frequency | !,! | ! | 12 | ix=192,pal=11 | 37 | 1290 | intensity,aux0,aux1,step,call | setPixelColorXY, getPixelColorXY, setPixelColor, getPixelColor, fade_out, blur, color_from_palette, is2D, XY, SEGLEN, hw_random8, hw_random16, random8, random16 |
| 43 | Rain | mode_rain | 1D+2D | 1D+2D | no | !,Spawning rate | !,! | ! | 12 | ix=128,pal=0 | 31 | 1330 | aux0,aux1,step,call | setPixelColorXY, getPixelColorXY, setPixelColor, getPixelColor, move, is2D, XY, SEGLEN |
| 44 | Tetrix | mode_tetrix | 1D | 1D | no | !,Width,,,,One color | !,! | ! | (none=1D) | sx=0,ix=0,pal=11,m12=1 | 72 | 3961 | speed,intensity,check1,call,data | setPixelColor, blendPixelColor, fill, fade_out, blur, color_from_palette, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, strip.now |
| 45 | Fire Flicker | mode_fire_flicker | 1D | 0D+1D | no | !,! | ! | ! | 01 |  | 22 | 1366 | speed,intensity,step | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, hw_random8, random8, strip.now |
| 46 | Gradient | mode_gradient | 1D | 1D | no | !,Spread | !,! | ! | (none=1D) | ix=16 | 3 | 1420 | none | none |
| 47 | Loading | mode_loading | 1D | 1D | no | !,Fade | !,! | ! | (none=1D) | ix=16 | 3 | 1429 | none | none |
| 48 | Rolling Balls | mode_rolling_balls | 1D | 1D | no | !,# of balls,,,,Collide,Overlay,Trails | !,!,! | ! | 1 | m12=1 | 83 | 3052 | speed,intensity,check1,check2,check3,call,data | setPixelColor, fill, fade_out, color_from_palette, color_wheel, allocateData, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, scale8, strip.now |
| 49 | Fairy | mode_fairy | 1D | 1D | no | !,# of flashers | !,! | ! | (none=1D) |  | 70 | 1470 | speed,intensity,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, gamma8, strip.now |
| 50 | Two Dots | mode_two_dots | 1D | 1D | no | !,Dot size,,,,,Overlay | 1,2,Bg | ! | (none=1D) |  | 17 | 1437 | speed,intensity,check2 | setPixelColor, fill, SEGCOLOR, SEGLEN, strip.now |
| 51 | Fairytwinkle | mode_fairytwinkle | 1D | 1D | no | !,! | !,! | ! | (none=1D) | m12=0 | 42 | 1547 | speed,intensity,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, gamma8, strip.now |
| 52 | Running Dual | mode_running_dual | 1D | 1D | no | !,Wave width | L,!,R | ! | (none=1D) |  | 3 | 627 | none | none |
| 53 | Image | mode_image | 1D+2D | 1D+2D | no | !,Blur, |  |  | 12 | sx=128,ix=0 | 11 | 4642 | none | none |
| 54 | Chase 3 | mode_tricolor_chase | 1D | 1D | no | !,Size | 1,2,3 | ! | (none=1D) |  | 3 | 1616 | none | SEGCOLOR |
| 55 | Tri Wipe | mode_tricolor_wipe | 1D | 1D | no | ! | 1,2,3 | ! | (none=1D) |  | 32 | 1697 | speed | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 56 | Tri Fade | mode_tricolor_fade | 1D | 1D | no | ! | 1,2,3 | ! | (none=1D) |  | 34 | 1737 | speed | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, strip.now |
| 57 | Lightning | mode_lightning | 1D | 1D | no | !,!,,,,,Overlay | !,! | ! | (none=1D) |  | 39 | 1906 | speed,intensity,check2,aux0,aux1,step | setPixelColor, fill, color_from_palette, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now |
| 58 | ICU | mode_icu | 1D | 1D | no | !,!,,,,,Overlay | !,! | ! | (none=1D) |  | 66 | 1625 | intensity,check2,aux0,aux1,step | setPixelColor, fill, color_from_palette, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now |
| 59 | Multi Comet | mode_multi_comet | 1D | 1D | no | !,Fade | !,! | ! | 1 |  | 30 | 1778 | speed,intensity,step,data | setPixelColor, fade_out, color_from_palette, allocateData, SEGCOLOR, SEGLEN, hw_random16, random16, strip.now |
| 60 | Scanner Dual | mode_dual_larson_scanner | 1D | 1D | no | !,Trail,Delay,,,Dual,Bi-delay | !,!,! | ! | (none=1D) | m12=0,c1=0 | 4 | 1256 | check1 | none |
| 61 | Stream 2 | mode_random_chase | 1D | 1D | no | ! |  |  | (none=1D) |  | 27 | 1815 | speed,aux0,aux1,step,call | setPixelColor, SEGLEN, random8, random16, RGBW32, strip.now |
| 62 | Oscillate | mode_oscillate | 1D | 1D | no | (none) |  |  | (none=1D) |  | 47 | 1856 | speed,intensity,step,call,data | setPixelColor, color_blend, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, strip.now |
| 63 | Pride 2015 | mode_pride_2015 | 1D | 1D | no | ! |  |  | (none=1D) |  | 3 | 1999 | none | none |
| 64 | Juggle | mode_juggle | 1D | 1D | no | !,Trail |  | ! | (none=1D) | sx=64,ix=128 | 14 | 2014 | speed,intensity | setPixelColor, getPixelColor, fadeToBlackBy, ColorFromPalette, SEGPALETTE, SEGLEN, beatsin88_t, CRGB, CHSV |
| 65 | Palette | mode_palette | 1D+2D | 1D+2D | no | Shift,Size,Rotation,,,Animate Shift,Animate Rotation,Anamorphic |  | ! | 12 | ix=112,c1=0,o1=1,o2=0,o3=1 | 95 | 2031 | speed,intensity,custom1,check1,check2,check3 | setPixelColorXY, setPixelColor, color_wheel, XY, sin16_t, cos16_t, sin_t, cos_t, strip.now, strip.isMatrix |
| 66 | Fire 2012 | mode_fire_2012 | 1D | 1D | no | Cooling,Spark rate,,2D Blur,Boost |  | ! | 1 | pal=35,sx=64,ix=160,m12=1,c2=128 | 55 | 2157 | speed,intensity,custom2,custom3,step,data | setPixelColor, blurCol, blur, ColorFromPalette, allocateData, is2D, SEGPALETTE, SEGLEN, hw_random8, random8, qadd8, qsub8, strip.now |
| 67 | Colorwaves | mode_colorwaves | 1D | 1D | no | !,Hue | ! | ! | (none=1D) | pal=26 | 3 | 2007 | none | none |
| 68 | Bpm | mode_bpm | 1D | 1D | no | ! | ! | ! | (none=1D) | sx=64 | 7 | 2216 | speed | setPixelColor, color_from_palette, SEGLEN, sin8_t, beatsin8_t, strip.now |
| 69 | Fill Noise | mode_fillnoise8 | 1D | 1D | no | ! | ! | ! | (none=1D) |  | 8 | 2226 | speed,step,call | setPixelColor, color_from_palette, SEGLEN, sin8_t, beatsin8_t, perlin8, hw_random |
| 70 | Noise 1 | mode_noise16_1 | 1D | 1D | no | ! | ! | ! | (none=1D) | pal=20 | 16 | 2237 | speed,step | setPixelColor, color_from_palette, SEGLEN, sin8_t, beatsin8_t, perlin16 |
| 71 | Noise 2 | mode_noise16_2 | 1D | 1D | no | ! | ! | ! | (none=1D) | pal=43 | 13 | 2256 | speed,step | setPixelColor, color_from_palette, SEGLEN, sin8_t, perlin16 |
| 72 | Noise 3 | mode_noise16_3 | 1D | 1D | no | ! | ! | ! | (none=1D) | pal=35 | 16 | 2272 | speed,step | setPixelColor, color_from_palette, SEGLEN, sin8_t, perlin16 |
| 73 | Noise 4 | mode_noise16_4 | 1D | 1D | no | ! | ! | ! | (none=1D) | pal=26 | 7 | 2292 | speed | setPixelColor, color_from_palette, SEGLEN, perlin16, strip.now |
| 74 | Colortwinkles | mode_colortwinkle | 1D | 1D | no | Fade speed,Spawn speed |  | ! | (none=1D) | m12=0 | 52 | 2303 | speed,intensity,step,data | setPixelColor, getPixelColor, ColorFromPalette, allocateData, SEGPALETTE, SEGLEN, hw_random8, hw_random16, random8, random16, CRGB, gamma8, strip.now |
| 75 | Lake | mode_lake | 1D | 1D | no | ! | Fx | ! | (none=1D) |  | 13 | 2359 | speed | setPixelColor, color_from_palette, SEGLEN, sin8_t, cos8_t, beatsin8_t, cubicwave8 |
| 76 | Meteor | mode_meteor | 1D | 1D | no | !,Trail,,,,Gradient,,Smooth |  | ! | 1 |  | 64 | 2378 | speed,intensity,check1,check3,step,data | setPixelColor, color_from_palette, allocateData, SEGLEN, hw_random8, random8, scale8, strip.now |
| 77 | Copy Segment | mode_copy_segment | 1D+2D | 1D+2D | no | ,Color shift,Lighten,Brighten,ID,Axis(2D),FullStack(last frame) |  |  | 12 | ix=0,c1=0,c2=0,c3=0 | 43 | 144 | intensity,custom1,custom2,custom3,check1,check2 | setPixelColorXY, getPixelColorXY, setPixelColor, getPixelColor, fadeToBlackBy, is2D, XY, CRGB |
| 78 | Railway | mode_railway | 1D | 1D | no | !,Smoothness | 1,2 | ! | (none=1D) | pal=3 | 27 | 2446 | speed,intensity,aux0,step | setPixelColor, color_from_palette, SEGLEN |
| 79 | Ripple | mode_ripple | 1D+2D | 1D+2D | no | !,Wave #,Blur,,,,Overlay | ,! | ! | 12 | c1=0 | 9 | 2545 | custom1,check2 | fill, fade_out, SEGCOLOR, SEGLEN |
| 80 | Twinklefox | mode_twinklefox | 1D | 1D | no | !,Twinkle rate,,,,Cool | !,! | ! | (none=1D) |  | 4 | 2700 | none | none |
| 81 | Twinklecat | mode_twinklecat | 1D | 1D | no | !,Twinkle rate,,,,Cool,Reverse | !,! | ! | (none=1D) |  | 4 | 2707 | none | none |
| 82 | Halloween Eyes | mode_halloween_eyes | 1D+2D | 1D+2D | no | Eye off time,Eye on time,,,,,Overlay | !,! | ! | 12 |  | 152 | 2714 | speed,intensity,check2,data | setPixelColorXY, setPixelColor, fill, color_from_palette, color_blend, allocateData, XY, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now, strip.isMatrix |
| 83 | Solid Pattern | mode_static_pattern | 1D | 1D | no | Fg size,Bg size | Fg,! | ! | (none=1D) | pal=0 | 16 | 2870 | speed,intensity | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN |
| 84 | Solid Pattern Tri | mode_tri_static_pattern | 1D | 1D | no | ,Size | 1,2,3 |  | (none=1D) | pal=0 | 21 | 2889 | intensity | setPixelColor, SEGCOLOR, SEGLEN |
| 85 | Spots | mode_spots | 1D | 1D | no | Spread,Width,,,,,Overlay | !,! | ! | (none=1D) |  | 4 | 2940 | speed | none |
| 86 | Spots Fade | mode_spots_fade | 1D | 1D | no | Spread,Width,,,,,Overlay | !,! | ! | (none=1D) |  | 7 | 2948 | speed | triwave16, strip.now |
| 87 | Glitter | mode_glitter | 1D | 1D | no | !,!,,,,,Overlay | ,,Glitter color | ! | (none=1D) | pal=11,m12=0 | 18 | 3392 | speed,intensity,check2 | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN, strip.now |
| 88 | Candle | mode_candle | 1D | 0D+1D | no | !,! | !,! | ! | 01 | sx=96,ix=224,pal=0 | 4 | 3579 | none | none |
| 89 | Fireworks Starburst | mode_starburst | 1D | 1D | no | Chance,Fragments,,,,,Overlay | ,! | ! | (none=1D) | pal=11,m12=0 | 108 | 3613 | speed,intensity,check2,data | setPixelColor, fill, color_wheel, color_blend, allocateData, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, CRGB, RGBW32, strip.now |
| 90 | Fireworks 1D | mode_exploding_fireworks | 1D+2D | 1D+2D | no | Gravity,Firing side | !,! | ! | 12 | pal=11,ix=128 | 128 | 3731 | speed,intensity,check3,aux0,aux1,data | setPixelColorXY, setPixelColor, fade_out, blur, color_wheel, color_blend, allocateData, is2D, XY, SEGCOLOR, SEGLEN, hw_random8, hw_random16, random8, random16, CRGB, qsub8 |
| 91 | Bouncing Balls | mode_bouncing_balls | 1D | 1D | no | Gravity,# of balls,,,,,Overlay | !,!,! | ! | 1 | m12=1 | 69 | 2967 | speed,intensity,check2,call,data | setPixelColor, fill, fade_out, blur, color_wheel, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8, strip.now |
| 92 | Sinelon | mode_sinelon | 1D | 1D | no | !,Trail | !,!,! | ! | (none=1D) |  | 3 | 3369 | none | none |
| 93 | Sinelon Dual | mode_sinelon_dual | 1D | 1D | no | !,Trail | !,!,! | ! | (none=1D) |  | 3 | 3375 | none | none |
| 94 | Sinelon Rainbow | mode_sinelon_rainbow | 1D | 1D | no | !,Trail | ,,! | ! | (none=1D) |  | 3 | 3381 | none | none |
| 95 | Popcorn | mode_popcorn | 1D | 1D | no | !,!,,,,,Overlay | !,!,! | ! | (none=1D) | m12=1 | 57 | 3435 | speed,intensity,check2,data | setPixelColor, fill, color_wheel, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8 |
| 96 | Drip | mode_drip | 1D | 1D | no | Gravity,# of drips,,,,,Overlay | !,! | ! | (none=1D) | m12=1 | 78 | 3867 | speed,intensity,check2,data | setPixelColor, fill, color_blend, allocateData, SEGCOLOR, SEGLEN, hw_random8, random8 |
| 97 | Plasma | mode_plasma | 1D | 1D | no | Phase,! | ! | ! | (none=1D) |  | 15 | 4040 | speed,intensity,aux0,call | setPixelColor, color_from_palette, SEGLEN, sin8_t, cos8_t, beatsin8_t, cubicwave8, hw_random8, random8, qsub8 |
| 98 | Percent | mode_percent | 1D | 1D | no | !,% of fill,,,,One color | !,! | ! | (none=1D) |  | 44 | 4062 | speed,intensity,check1,aux1 | setPixelColor, color_from_palette, SEGCOLOR, SEGLEN |
| 99 | Ripple Rainbow | mode_ripple_rainbow | 1D+2D | 1D+2D | no | !,Wave # |  | ! | 12 |  | 16 | 2557 | aux0,aux1,call | fill, color_wheel, color_blend, SEGLEN, hw_random8, random8 |
| 100 | Heartbeat | mode_heartbeat | 1D | 0D+1D | no | !,! | !,! | ! | 01 | m12=1 | 24 | 4113 | speed,intensity,aux0,aux1,step | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, strip.now |
| 101 | Pacifica | mode_pacifica | 1D | 1D | no | !,Angle |  | ! | (none=1D) | pal=51 | 73 | 4180 | speed,aux0,aux1,step | setPixelColor, fill, SEGPALETTE, SEGLEN, sin8_t, sin16_t, beatsin8_t, beatsin16_t, beatsin88_t, CRGB, scale8, qadd8, strip.now |
| 102 | Candle Multi | mode_candle_multi | 1D | 1D | no | !,! | !,! | ! | (none=1D) | sx=96,ix=224,pal=0 | 4 | 3586 | none | none |
| 103 | Solid Glitter | mode_solid_glitter | 1D | 1D | no | ,! | Bg,,Glitter color |  | (none=1D) | m12=0 | 5 | 3414 | intensity | fill, SEGCOLOR |
| 104 | Sunrise | mode_sunrise | 1D | 1D | no | Time [min],Width |  | ! | (none=1D) | pal=35,sx=60 | 43 | 4259 | speed,intensity,aux0,step,call | setPixelColor, fill, color_from_palette, SEGLEN, triwave16, strip.now |
| 105 | Phased | mode_phased | 1D | 1D | no | !,! | !,! | ! | (none=1D) |  | 3 | 4332 | none | none |
| 106 | Twinkleup | mode_twinkleup | 1D | 1D | no | !,Intensity | !,! | ! | (none=1D) | m12=0 | 13 | 4344 | speed,intensity | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, sin8_t, random8, strip.now |
| 107 | Noise Pal | mode_noisepal | 1D | 1D | no | !,Scale |  | ! | (none=1D) |  | 31 | 4361 | speed,intensity,aux0,step,data | setPixelColor, ColorFromPalette, allocateData, SEGPALETTE, SEGLEN, sin8_t, beatsin8_t, perlin8, hw_random8, random8, CRGB, CHSV, gamma8, strip.now |
| 108 | Sine | mode_sinewave | 1D | 1D | no | !,Scale |  | ! | (none=1D) |  | 14 | 4398 | speed,intensity,step | setPixelColor, color_from_palette, color_blend, SEGCOLOR, SEGLEN, cubicwave8, strip.now |
| 109 | Phased Noise | mode_phased_noise | 1D | 1D | no | !,! | !,! | ! | (none=1D) |  | 3 | 4338 | none | none |
| 110 | Flow | mode_flow | 1D | 1D | no | !,Zones |  | ! | (none=1D) | m12=1 | 30 | 4418 | speed,intensity | setPixelColor, color_from_palette, SEGLEN, strip.now |
| 111 | Chunchun | mode_chunchun | 1D | 1D | no | !,Gap size | !,! | ! | (none=1D) |  | 17 | 4455 | speed,intensity | setPixelColor, fade_out, color_from_palette, SEGLEN, sin16_t, strip.now |
| 112 | Dancing Shadows | mode_dancing_shadows | 1D | 1D | no | !,# of shadows | ! | ! | (none=1D) |  | 113 | 4505 | speed,intensity,aux0,data | blendPixelColor, fill, color_from_palette, allocateData, SEGLEN, cubicwave8, hw_random8, hw_random16, random8, random16, strip.now |
| 113 | Washing Machine | mode_washing_machine | 1D | 1D | no | !,! |  | ! | (none=1D) |  | 10 | 4625 | speed,intensity,step | setPixelColor, color_from_palette, SEGLEN, sin8_t, strip.now |
| 114 | Rotozoomer | mode_2Dplasmarotozoom | 2D | 2D | no | !,Scale,,,,Alt |  | ! | 2 | pal=54 | 38 | 6600 | speed,intensity,check1,data | setPixelColorXY, setPixelColor, color_from_palette, allocateData, is2D, XY, sin_t, cos_t, strip.now, strip.isMatrix |
| 115 | Blends | mode_blends | 1D | 1D | no | Shift speed,Blend speed |  | ! | (none=1D) |  | 19 | 4659 | speed,intensity,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGLEN, quadwave8, strip.now |
| 116 | TV Simulator | mode_tv_simulator | 1D | 0D+1D | no | !,! |  | ! | 01 |  | 99 | 4706 | speed,intensity,aux0,aux1,data | setPixelColor, allocateData, SEGLEN, hw_random8, hw_random16, random8, random16, strip.now |
| 117 | Dynamic Smooth | mode_dynamic_smooth | 1D | 1D | no | !,! |  | ! | (none=1D) |  | 6 | 420 | check1 | none |
| 118 | Spaceships | mode_2Dspaceships | 2D | 2D | no | !,Blur,,,,Smear |  | ! | 2 |  | 33 | 6086 | speed,intensity,check1,aux0,step | addPixelColorXY, addPixelColor, fadeToBlackBy, blur, move, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, hw_random8, random8, strip.now, strip.isMatrix |
| 119 | Crazy Bees | mode_2Dcrazybees | 2D | 2D | no | !,Blur,,,,Smear |  | ! | 2 | pal=11,ix=0 | 63 | 6127 | speed,intensity,check1,step,call,data | setPixelColorXY, addPixelColorXY, setPixelColor, addPixelColor, fadeToBlackBy, blur, color_from_palette, allocateData, is2D, XY, random8, CRGB, CHSV, strip.now, strip.isMatrix |
| 120 | Ghost Rider | mode_2Dghostrider | 2D | 2D | no | Fade rate,Blur |  | ! | 2 |  | 79 | 6199 | speed,intensity,aux0,aux1,step,data | fadeToBlackBy, blur, wu_pixel, ColorFromPalette, allocateData, is2D, SEGPALETTE, sin_t, cos_t, hw_random8, hw_random16, random8, random16, CRGB, strip.now, strip.isMatrix |
| 121 | Blobs | mode_2Dfloatingblobs | 2D | 2D | no | !,# blobs,Blur,Trail | ! | ! | 2 | c1=8 | 89 | 6286 | speed,intensity,custom1,custom2,aux0,aux1,step,data | setPixelColorXY, setPixelColor, fill, fadeToBlackBy, blur, fillCircle, color_from_palette, allocateData, is2D, XY, hw_random8, random8, strip.now, strip.isMatrix |
| 122 | Scrolling Text | mode_2Dscrollingtext | 2D | 2D | no | !,Y Offset,Trail,Font size,Rotate,Gradient,Custom Font,Reverse | !,!,Gradient | ! | 2 | ix=128,c1=0,rev=0,mi=0,rY=0,mY=0 | 185 | 6382 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step | fade_out, color_from_palette, is2D, SEGCOLOR, strip.now, strip.isMatrix |
| 123 | Drift Rose | mode_2Ddriftrose | 2D | 2D | no | Fade,Blur,,,,Smear |  | ! | 2 | pal=11 | 20 | 6574 | speed,intensity,check1 | fadeToBlackBy, blur, wu_pixel, ColorFromPalette, is2D, SEGPALETTE, sin8_t, sin_t, cos_t, beatsin8_t, CHSV, strip.isMatrix |
| 124 | Distortion Waves | mode_2Ddistortionwaves | 2D | 2D | no | !,Scale,,,,Fill,Zoom,Alt |  | ! | 2 | pal=0 | 73 | 7754 | speed,intensity,check1,check2,check3 | setPixelColorXY, setPixelColor, blur, ColorFromPalette, is2D, XY, SEGPALETTE, cos8_t, sin16_t, beatsin16_t, CRGB, CHSV, RGBW32, strip.now, strip.isMatrix |
| 125 | Soap | mode_2Dsoap | 2D | 2D | no | !,Smoothness,Density |  | ! | 2 | pal=11 | 44 | 7886 | speed,intensity,aux0,aux1,call,data | setPixelColorXY, setPixelColor, ColorFromPalette, allocateData, is2D, XY, SEGPALETTE, perlin16, hw_random, CRGB, scale8, strip.isMatrix |
| 126 | Octopus | mode_2Doctopus | 2D | 2D | no | !,,Offset X,Offset Y,Legs,fasttan |  | ! | 2 |  | 51 | 7937 | speed,custom1,custom2,custom3,aux0,aux1,step,call,data | setPixelColorXY, setPixelColor, ColorFromPalette, allocateData, is2D, XY, SEGPALETTE, sin8_t, atan2_t, CRGB, CHSV, strip.isMatrix |
| 127 | Waving Cell | mode_2Dwavingcell | 2D | 2D | no | !,Blur,Amplitude 1,Amplitude 2,Amplitude 3,,Flow |  | ! | 2 | ix=0 | 19 | 7994 | speed,intensity,custom1,custom2,custom3,check2 | setPixelColorXY, setPixelColor, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, cos8_t, strip.now, strip.isMatrix |
| 128 | Pixels | mode_pixels | audio-reactive volume | 1D | volume | Fade rate,# of pixels | !,! | ! | 1v | m12=0,si=0 | 21 | 7173 | speed,intensity,data | setPixelColor, fade_out, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, um_data, hw_random16, random16, strip.now |
| 129 | Pixelwave | mode_pixelwave | audio-reactive volume | 1D | volume | !,Sensitivity | !,! | ! | 1v | ix=64,m12=2,si=0 | 22 | 7061 | speed,intensity,aux0,call | setPixelColor, getPixelColor, fill, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, strip.now |
| 130 | Juggles | mode_juggles | audio-reactive volume | 0D+1D | volume | !,# of balls | !,! | ! | 01v | m12=0,si=0 | 12 | 6924 | speed,intensity | setPixelColor, fade_out, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, sin16_t, beatsin16_t, strip.now |
| 131 | Matripix | mode_matripix | audio-reactive volume | 1D | volume | !,Brightness | !,! | ! | 1v | ix=64,m12=2,si=1 | 28 | 6942 | speed,intensity,aux0,call,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, um_data, getAudioData, strip.now |
| 132 | Gravimeter | mode_gravimeter | audio-reactive volume | 1D | volume | Rate of fall,Sensitivity | !,! | ! | 1v | ix=128,m12=2,si=0 | 3 | 6906 | none | none |
| 133 | Plasmoid | mode_plasmoid | audio-reactive volume | 0D+1D | volume | Phase,# of pixels | !,! | ! | 01v | sx=128,ix=128,m12=0,si=0 | 24 | 7094 | speed,intensity,data | addPixelColor, fadeToBlackBy, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, um_data, getAudioData, sin8_t, cos8_t, beatsin8_t, cubicwave8 |
| 134 | Puddles | mode_puddles | audio-reactive volume | 1D | volume | Fade rate,Puddle size | !,! | ! | 1v | m12=0,si=0 | 3 | 7164 | none | none |
| 135 | Midnoise | mode_midnoise | audio-reactive volume | 1D | volume | Fade rate,Max. length | !,! | ! | 1v | ix=128,m12=1,si=0 | 24 | 6976 | speed,intensity,aux0,aux1 | setPixelColor, fade_out, color_from_palette, SEGLEN, um_data, getAudioData, sin8_t, beatsin8_t, perlin8 |
| 136 | Noisemeter | mode_noisemeter | audio-reactive volume | 1D | volume | Fade rate,Width | !,! | ! | 1v | ix=128,m12=2,si=0 | 23 | 7032 | speed,intensity,aux0,aux1 | setPixelColor, fade_out, color_from_palette, SEGLEN, um_data, getAudioData, sin8_t, beatsin8_t, perlin8 |
| 137 | Freqwave | mode_freqwave | audio-reactive FFT | 0D+1D | FFT | Speed,Sound effect,Low bin,High bin,Pre-amp |  |  | 01f | m12=2,si=0 | 43 | 7384 | speed,intensity,custom1,custom2,custom3,aux0,call | setPixelColor, getPixelColor, fill, SEGLEN, um_data, getAudioData, CRGB, CHSV, gamma8 |
| 138 | Freqmatrix | mode_freqmatrix | audio-reactive FFT | 0D+1D | FFT | Speed,Sound effect,Low bin,High bin,Sensitivity |  |  | 01f | m12=3,si=0 | 44 | 7290 | speed,intensity,custom1,custom2,custom3,aux0,call | setPixelColor, getPixelColor, fill, SEGLEN, um_data, getAudioData, CRGB, CHSV, gamma8 |
| 139 | GEQ | mode_2DGEQ | audio-reactive FFT | 2D | FFT | Fade speed,Ripple decay,# of bands,,Bin,Color bars | !,,Peaks | ! | 2f | c1=255,c2=64,pal=11,si=0,c3=0 | 53 | 7542 | speed,intensity,custom1,custom3,check1,step,call,data | setPixelColorXY, setPixelColor, fadeToBlackBy, color_from_palette, allocateData, is2D, XY, SEGCOLOR, um_data, getAudioData, strip.now, strip.isMatrix |
| 140 | Waterfall | mode_waterfall | audio-reactive FFT | 0D+1D | FFT | !,Adjust color,Select bin,Volume (min) | !,! | ! | 01f | c2=0,m12=2,si=0 | 47 | 7488 | speed,intensity,custom1,custom2,aux0,call,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, um_data, getAudioData, CRGB, CHSV, gamma8 |
| 141 | Freqpixels | mode_freqpixels | audio-reactive FFT | 1D | FFT | Fade rate,Starting color and # of pixels | !,!, | ! | 1f | m12=0,si=0 | 22 | 7344 | speed,intensity,call | setPixelColor, fade_out, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, hw_random16, random16 |
| 143 | Noisefire | mode_noisefire | audio-reactive volume | 0D+1D | volume | !,! |  |  | 01v | m12=2,si=0 | 19 | 7007 | speed,intensity,call | setPixelColor, fill, ColorFromPalette, SEGLEN, um_data, getAudioData, perlin8, CRGB, CHSV, strip.now |
| 144 | Puddlepeak | mode_puddlepeak | audio-reactive volume | 1D | volume | Fade rate,Puddle size,Select bin,Volume (min) | !,! | ! | 1v | c2=0,m12=0,si=0 | 3 | 7159 | none | none |
| 145 | Noisemove | mode_noisemove | audio-reactive FFT | 0D+1D | FFT | Move speed,Fade rate | !,! | ! | 01f | m12=0,si=0 | 15 | 7433 | speed,intensity,call | setPixelColor, fadeToBlackBy, move, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, perlin16, strip.now |
| 146 | Noise2D | mode_2Dnoise | 2D | 2D | no | !,Scale |  | ! | 2 |  | 15 | 5834 | speed,intensity | setPixelColorXY, setPixelColor, ColorFromPalette, is2D, XY, SEGPALETTE, perlin8, strip.now, strip.isMatrix |
| 147 | Perlin Move | mode_perlinmove | 1D | 1D | no | !,# of pixels,Fade rate | !,! | ! | (none=1D) |  | 9 | 5036 | speed,intensity,custom1 | setPixelColor, fade_out, move, color_from_palette, SEGLEN, perlin16, strip.now |
| 148 | Ripple Peak | mode_ripplepeak | audio-reactive volume | 1D | volume | Fade rate,Max # of ripples,Select bin,Volume (min) | !,! | ! | 1v | c2=0,m12=0,si=0 | 66 | 6651 | intensity,custom1,custom2,call,data | setPixelColor, fade_out, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, um_data, getAudioData, hw_random8, hw_random16, random8, random16 |
| 149 | Firenoise | mode_2Dfirenoise | 2D | 2D | no | X scale,Y scale,,,,Palette |  | ! | 2 | pal=66 | 25 | 5335 | speed,intensity,check1,call | setPixelColorXY, setPixelColor, fill, ColorFromPalette, is2D, XY, SEGPALETTE, perlin8, CRGB, strip.now, strip.isMatrix |
| 150 | Squared Swirl | mode_2Dsquaredswirl | 2D | 2D | no | ,Fade,,,Blur |  | ! | 2 |  | 24 | 5971 | intensity,custom3 | addPixelColorXY, addPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, strip.now, strip.isMatrix |
| 151 | PacMan | mode_pacman | 1D | 1D | no | Speed,# of PowerDots,Blink distance,Blur,# of Ghosts,Dots,Smear,Compact |  | ! | 1 | m12=0,sx=192,ix=64,c1=64,c2=0,c3=12,o1=1,o2=0 | 169 | 3161 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | setPixelColor, fill, blur, allocateData, SEGLEN, strip.now |
| 152 | DNA | mode_2Ddna | 2D | 2D | no | Scroll speed,Blur,,,,Smear |  | ! | 2 | ix=0 | 13 | 5243 | speed,intensity,check1 | setPixelColorXY, setPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, strip.now, strip.isMatrix |
| 153 | Matrix | mode_2Dmatrix | 2D | 2D | no | !,Spawning rate,Trail,,,Custom color | Spawn,Trail |  | 2 |  | 63 | 5708 | speed,intensity,custom1,check1,step,call,data | setPixelColorXY, setPixelColor, fill, fadeToBlackBy, allocateData, is2D, XY, SEGCOLOR, hw_random8, random8, RGBW32, gamma8, strip.now, strip.isMatrix |
| 154 | Metaballs | mode_2Dmetaballs | 2D | 2D | no | ! |  | ! | 2 |  | 51 | 5777 | speed | setPixelColorXY, setPixelColor, color_from_palette, is2D, XY, sin8_t, sqrt32_bw, beatsin8_t, perlin8, strip.now, strip.isMatrix |
| 155 | Freqmap | mode_freqmap | audio-reactive FFT | 1D | FFT | Fade rate,Starting color | !,! | ! | 1f | m12=0,si=0 | 25 | 7259 | speed,intensity,call | setPixelColor, fill, fade_out, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData |
| 156 | Gravcenter | mode_gravcenter | audio-reactive volume | 1D | volume | Rate of fall,Sensitivity | !,! | ! | 1v | ix=128,m12=2,si=0 | 3 | 6889 | none | none |
| 157 | Gravcentric | mode_gravcentric | audio-reactive volume | 1D | volume | Rate of fall,Sensitivity | !,! | ! | 1v | ix=128,m12=3,si=0 | 3 | 6897 | none | none |
| 158 | Gravfreq | mode_gravfreq | audio-reactive FFT | 1D | FFT | Rate of fall,Sensitivity | !,! | ! | 1f | ix=128,m12=0,si=0 | 3 | 6915 | none | none |
| 159 | DJ Light | mode_DJLight | audio-reactive FFT | 0D+1D | FFT | Speed |  |  | 01f | m12=2,si=0 | 23 | 7230 | speed,aux0,call | setPixelColor, getPixelColor, fill, fadeToBlackBy, SEGLEN, um_data, getAudioData, CRGB, gamma8 |
| 160 | Funky Plank | mode_2DFunkyPlank | audio-reactive FFT | 2D | FFT | Scroll speed,,# of bands |  |  | 2f | si=0 | 45 | 7601 | speed,custom1,aux0,call | setPixelColorXY, getPixelColorXY, setPixelColor, getPixelColor, fill, is2D, XY, um_data, getAudioData, CHSV, gamma8, strip.isMatrix |
| 161 | Shimmer | mode_shimmer | 1D | 1D | no | Speed,Interval,Size,Granular,Flow,Zebra,Reverse,Sporadic | Fx,Bg,Cx | ! | 1 | pal=15,sx=220,ix=10,c2=0,c3=0 | 64 | 5089 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | setPixelColor, color_from_palette, color_blend, allocateData, SEGCOLOR, SEGLEN, sin16_t, perlin16, hw_random, strip.now |
| 162 | Pulser | mode_2DPulser | 2D | 2D | no | !,Blur |  | ! | 2 |  | 14 | 5923 | speed,intensity | setPixelColorXY, setPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, strip.now, strip.isMatrix |
| 163 | Blurz | mode_blurz | audio-reactive FFT | 1D | FFT | Fade rate,Blur | !,Color mix | ! | 1f | m12=0,si=0 | 25 | 7199 | speed,intensity,aux0,step,call | setPixelColor, fill, fade_out, blur, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, hw_random16, random16 |
| 164 | Drift | mode_2DDrift | 2D | 2D | no | Rotation speed,Blur,,,,Twin,Smear |  | ! | 2 | ix=0 | 22 | 5307 | speed,intensity,check1,check2 | setPixelColorXY, setPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin_t, cos_t, strip.now, strip.isMatrix |
| 165 | Waverly | mode_2DWaverly | audio-reactive volume | 2D | volume | Amplification,Sensitivity,,,,,Blur |  | ! | 2v | ix=64,si=0 | 28 | 6762 | speed,intensity,check3 | addPixelColorXY, addPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, um_data, getAudioData, perlin8, strip.now, strip.isMatrix |
| 166 | Sun Radiation | mode_2DSunradiation | 2D | 2D | no | Variance,Brightness |  |  | 2 |  | 42 | 6001 | speed,intensity,call,data | setPixelColorXY, setPixelColor, fill, allocateData, is2D, XY, perlin8, strip.now, strip.isMatrix |
| 167 | Colored Bursts | mode_2DColoredBursts | 2D | 2D | no | Speed,# of lines,,,Blur,Gradient,Smear,Dots |  | ! | 2 | c3=16 | 44 | 5193 | speed,intensity,custom3,check1,check2,check3,aux0,call | setPixelColorXY, addPixelColorXY, setPixelColor, addPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, strip.isMatrix |
| 168 | Julia | mode_2DJulia | 2D | 2D | no | ,Max iterations per pixel,X center,Y center,Area size, Blur | ! | ! | 2 | ix=24,c1=128,c2=128,c3=16 | 99 | 5576 | intensity,custom1,custom2,custom3,check1,call,data | setPixelColorXY, setPixelColor, blur, color_from_palette, allocateData, is2D, XY, sin16_t, strip.now, strip.isMatrix |
| 172 | Game Of Life | mode_2Dgameoflife | 2D | 2D | no | !,,Blur,,,,,Mutation | !,! | ! | 2 | pal=11,sx=128 | 150 | 5390 | speed,custom1,check3,aux0,aux1,step,call,data | setPixelColor, getPixelColor, color_from_palette, color_blend, allocateData, is2D, SEGCOLOR, hw_random8, random8, strip.now, strip.isMatrix |
| 173 | Tartan | mode_2Dtartan | 2D | 2D | no | X scale,Y scale,,,Sharpness |  | ! | 2 |  | 31 | 6049 | speed,intensity,custom3,call | setPixelColorXY, addPixelColorXY, setPixelColor, addPixelColor, fill, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, sin16_t, beatsin16_t, strip.isMatrix |
| 174 | Polar Lights | mode_2DPolarLights | 2D | 2D | no | !,Scale,,,,Flip Palette |  | ! | 2 | pal=71 | 26 | 5891 | speed,intensity,check1,step,call | setPixelColorXY, setPixelColor, fill, color_from_palette, is2D, XY, perlin8, qsub8, strip.isMatrix |
| 175 | Swirl | mode_2DSwirl | audio-reactive volume | 2D | volume | !,Sensitivity,Blur | ,Bg Swirl | ! | 2v | ix=64,si=0 | 30 | 6725 | speed,intensity,custom1,call | addPixelColorXY, addPixelColor, fill, blur, ColorFromPalette, is2D, XY, SEGPALETTE, um_data, getAudioData, sin8_t, beatsin8_t, CHSV, strip.now, strip.isMatrix |
| 176 | Lissajous | mode_2DLissajous | 2D | 2D | no | X frequency,Fade rate,Blur,,Speed,Smear | ! | ! | 2 | c1=0 | 21 | 5681 | speed,intensity,custom1,custom3,check1 | setPixelColorXY, setPixelColor, fadeToBlackBy, blur, color_from_palette, is2D, XY, sin8_t, cos8_t, strip.now, strip.isMatrix |
| 177 | Frizzles | mode_2DFrizzles | 2D | 2D | no | X frequency,Y frequency,Blur,,,Smear |  | ! | 2 |  | 14 | 5366 | speed,intensity,custom1,check1 | addPixelColorXY, addPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, strip.isMatrix |
| 178 | Plasma Ball | mode_2DPlasmaball | 2D | 2D | no | Speed,,Fade,Blur |  | ! | 2 |  | 29 | 5855 | speed,custom1,custom2 | addPixelColorXY, addPixelColor, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, perlin8, CRGB, strip.now, strip.isMatrix |
| 179 | Flow Stripe | mode_FlowStripe | 1D | 1D | no | Hue speed,Effect speed |  | ! | (none=1D) | pal=11 | 14 | 5068 | speed,intensity | setPixelColor, color_from_palette, SEGLEN, sin8_t, strip.now |
| 180 | Hiphotic | mode_2DHiphotic | 2D | 2D | no | X scale,Y scale,,,Speed | ! | ! | 2 |  | 13 | 5546 | speed,intensity,custom3 | setPixelColorXY, setPixelColor, color_from_palette, is2D, XY, sin8_t, cos8_t, strip.now, strip.isMatrix |
| 181 | Sindots | mode_2DSindots | 2D | 2D | no | !,Dot distance,Fade rate,Blur,,Smear |  | ! | 2 |  | 21 | 5943 | speed,intensity,custom1,custom2,check1,call | setPixelColorXY, setPixelColor, fill, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, strip.now, strip.isMatrix |
| 182 | DNA Spiral | mode_2DDNASpiral | 2D | 2D | no | Scroll speed,Y frequency,Blur,,,Smear |  | ! | 2 | c1=0 | 40 | 5261 | speed,intensity,custom1,check1,call | setPixelColorXY, addPixelColorXY, setPixelColor, addPixelColor, fill, fadeToBlackBy, blur, ColorFromPalette, is2D, XY, SEGPALETTE, sin8_t, beatsin8_t, scale8, strip.now, strip.isMatrix |
| 183 | Black Hole | mode_2DBlackHole | 2D | 2D | no | Fade rate,Outer Y freq.,Outer X freq.,Inner X freq.,Inner Y freq.,Solid,,Blur | ! | ! | 2 | pal=11 | 26 | 5161 | speed,intensity,custom1,custom2,custom3,check1,check3 | setPixelColorXY, addPixelColorXY, setPixelColor, addPixelColor, fadeToBlackBy, blur, color_from_palette, is2D, XY, sin8_t, beatsin8_t, strip.now, strip.isMatrix |
| 184 | Wavesins | mode_wavesins | 1D | 1D | no | !,Brightness variation,Starting color,Range of colors,Color variation | ! | ! | (none=1D) |  | 9 | 5052 | speed,intensity,custom1,custom2,custom3 | setPixelColor, color_from_palette, ColorFromPalette, SEGPALETTE, SEGLEN, sin8_t, beatsin8_t, strip.now |
| 185 | Rocktaves | mode_rocktaves | audio-reactive FFT | 0D+1D | FFT | (none) | !,! | ! | 01f | m12=1,si=0 | 27 | 7454 | none | addPixelColor, fadeToBlackBy, color_from_palette, color_blend, SEGCOLOR, SEGLEN, um_data, getAudioData, sin8_t, beatsin8_t |
| 186 | Akemi | mode_2DAkemi | audio-reactive FFT | 2D | FFT | Color speed,Dance | Head palette,Arms & Legs,Eyes & Mouth | Face palette | 2f | si=0 | 61 | 7687 | speed,intensity | setPixelColorXY, setPixelColor, color_from_palette, color_wheel, is2D, XY, SEGCOLOR, um_data, CRGB, strip.now, strip.isMatrix |
| 187 | PS Volcano | mode_particlevolcano | particle 2D | 2D | no | Speed,Intensity,Move,Bounce,Spread,AgeColor,Walls,Collide |  | ! | 2 | pal=35,sx=100,ix=190,c1=0,c2=160,c3=6,o1=1 | 63 | 8281 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 188 | PS Fire | mode_particlefire | particle 2D | 2D | no | Speed,Intensity,Flame Height,Wind,Spread,Smooth,Cylinder,Turbulence |  | ! | 2 | pal=35,sx=110,c1=110,c2=50,c3=31,o1=1 | 95 | 8352 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | ParticleSystem2D, initParticleSystem2D, perlin8, hw_random8, hw_random16, hw_random, random8, random16, strip.now |
| 189 | PS Fireworks | mode_particlefireworks | particle 2D | 2D | no | Launches,Explosion Size,Fuse,Blur,Gravity,Cylinder,Ground,Fast |  | ! | 2 | pal=11,ix=50,c1=40,c2=0,c3=12 | 133 | 8138 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, sin16_t, hw_random16, hw_random, random16 |
| 190 | PS Vortex | mode_particlevortex | particle 2D | 2D | no | Rotation Speed,Particle Speed,Arms,Flip,Nozzle,Smear,Direction,Random Flip |  | ! | 2 | pal=27,c1=200,c2=0,c3=0 | 104 | 8024 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | SEGLEN, ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 191 | PS Fuzzy Noise | mode_particleperlin | particle 2D | 2D | no | Speed,Particles,Bounce,Friction,Scale,Cylinder,Smear,Collide |  | ! | 2 | pal=64,sx=50,ix=200,c1=130,c2=30,c3=5 | 54 | 8681 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | ParticleSystem2D, initParticleSystem2D, perlin8, hw_random16, hw_random, random16 |
| 192 | PS Ballpit | mode_particlepit | particle 2D | 2D | no | Speed,Intensity,Size,Hardness,Saturation,Cylinder,Walls,Ground |  | ! | 2 | pal=11,sx=100,ix=220,c1=70,c2=180,c3=31,o3=1 | 61 | 8456 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, hw_random16, hw_random, random16 |
| 193 | PS Box | mode_particlebox | particle 2D | 2D | no | !,Particles,Tilt,Hardness,Size,Random,Washing Machine,Sloshing |  | ! | 2 | pal=53,ix=50,c3=1,o1=1 | 79 | 8595 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | ParticleSystem2D, initParticleSystem2D, sin16_t, cos16_t, perlin8, hw_random8, hw_random16, random8, random16, strip.now |
| 194 | PS Attractor | mode_particleattractor | particle 2D | 2D | no | Mass,Particles,Size,Collide,Friction,AgeColor,Move,Swallow |  | ! | 2 | pal=9,sx=100,ix=82,c1=2,c2=0 | 83 | 8850 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | um_data, ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 195 | PS Impact | mode_particleimpact | particle 2D | 2D | no | Launches,!,Force,Hardness,Blur,Cylinder,Walls,Collide |  | ! | 2 | pal=0,sx=32,ix=85,c1=70,c2=130,c3=0,o3=1 | 99 | 8742 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, hw_random16, hw_random, random16 |
| 196 | PS Waterfall | mode_particlewaterfall | particle 2D | 2D | no | Speed,Intensity,Variation,Collide,Position,Cylinder,Walls,Ground |  | ! | 2 | pal=9,sx=15,ix=200,c1=32,c2=160,o3=1 | 64 | 8524 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 197 | PS Spray | mode_particlespray | particle 2D | 2D | volume | Speed,!,Left/Right,Up/Down,Angle,Gravity,Cylinder/Square,Collide |  | ! | 2v | pal=0,sx=150,ix=150,c1=220,c2=30,c3=21 | 65 | 8941 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | um_data, ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 198 | PS GEQ 2D | mode_particleGEQ | particle 2D | 2D | FFT | Speed,Intensity,Diverge,Bounce,Gravity,Cylinder,Walls,Floor |  | ! | 2f | pal=0,sx=155,ix=200,c1=0 | 64 | 9014 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | um_data, getAudioData, ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 199 | PS GEQ Nova | mode_particlecenterGEQ | particle 2D | 2D | FFT | Speed,Intensity,Rotation Speed,Color Change,Nozzle,,Direction |  | ! | 2f | pal=13,ix=180,c1=0,c2=0,c3=8 | 62 | 9088 | speed,intensity,custom1,custom2,custom3,check2,aux0,call,data | um_data, getAudioData, ParticleSystem2D, initParticleSystem2D, hw_random16, random16 |
| 200 | PS Ghost Rider | mode_particleghostrider | particle 2D | 2D | no | Speed,Spiral,Blur,Color Cycle,Spread,AgeColor,Walls |  | ! | 2 | pal=1,sx=70,ix=0,c1=220,c2=30,c3=21,o1=1 | 73 | 9156 | speed,intensity,custom1,custom2,custom3,check1,check2,aux0,aux1,step,call,data | ParticleSystem2D, initParticleSystem2D, sin16_t, cos16_t, hw_random16, random16 |
| 201 | PS Blobs | mode_particleblobs | particle 2D | 2D | volume | Speed,Blobs,Size,Life,Blur,Wobble,Collide,Pulsate |  | ! | 2v | sx=30,ix=64,c1=200,c2=130,c3=0,o3=1 | 66 | 9236 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,call,data | um_data, ParticleSystem2D, initParticleSystem2D, hw_random16, hw_random, random16 |
| 202 | PS DripDrop | mode_particleDrip | particle 1D | 1D | no | Speed,!,Splash,Blur,Gravity,Rain,PushSplash,Smooth | ,! | ! | 1 | pal=0,sx=150,ix=25,c1=220,c2=30,c3=21 | 103 | 9415 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,call,data | ParticleSystem1D, initParticleSystem1D, hw_random8, hw_random16, hw_random, random8, random16 |
| 203 | PS Pinball | mode_particlePinball | particle 1D | 1D | no | Speed,!,Size,Blur,Gravity,Collide,Rolling,Position Color | ,! | ! | 1 | pal=0,ix=220,c2=0,c3=8,o1=1 | 107 | 9527 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | ParticleSystem1D, initParticleSystem1D, hw_random8, hw_random16, random8, random16 |
| 204 | PS Dancing Shadows | mode_particleDancingShadows | particle 1D | 1D | no | Speed,!,Blur,Color Cycle,,Smear,Position Color,Smooth | ,! | ! | 1 | sx=100,ix=180,c1=0,c2=0 | 107 | 9645 | speed,intensity,custom1,custom2,check1,check2,check3,aux0,call,data | ParticleSystem1D, initParticleSystem1D, cubicwave8, hw_random8, hw_random16, hw_random, random8, random16 |
| 205 | PS Fireworks 1D | mode_particleFireworks1D | particle 1D | 1D | no | Gravity,Explosion,Firing side,Blur,Color,Colorful,Trail,Smooth | ,! | ! | 1 | c2=30,o1=1 | 113 | 9759 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | ParticleSystem1D, initParticleSystem1D, hw_random8, hw_random16, random8, random16 |
| 206 | PS Sparkler | mode_particleSparkler | particle 1D | 1D | no | Move,!,Saturation,Blur,Sparklers,Slide,Bounce,Large | ,! | ! | 1 | pal=0,sx=255,c1=0,c2=0,c3=6 | 63 | 9879 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | ParticleSystem1D, initParticleSystem1D, hw_random16, hw_random, random16 |
| 207 | PS Hourglass | mode_particleHourglass | particle 1D | 1D | no | Interval,!,Color,Blur,Gravity,Colorflip,Start,Fast Reset | ,! | ! | 1 | pal=34,sx=5,ix=200,c1=140,c2=80,c3=4,o1=1,o2=1,o3=1 | 116 | 9949 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | ParticleSystem1D, initParticleSystem1D, strip.now |
| 208 | PS Spray 1D | mode_particle1Dspray | particle 1D | 1D | no | Speed(+/-),!,Position,Blur,Gravity(+/-),AgeColor,Bounce,Position Color | ,! | ! | 1 | sx=200,ix=220,c1=0,c2=0 | 43 | 10072 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,call,data | ParticleSystem1D, initParticleSystem1D, hw_random16, hw_random, random16 |
| 209 | PS 1D Balance | mode_particleBalance | particle 1D | 1D | no | !,!,Hardness,Blur,Tilt,Position Color,Wrap,Random | ,! | ! | 1 | pal=18,c2=0,c3=4,o1=1 | 73 | 10122 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,call,data | ParticleSystem1D, initParticleSystem1D, cos8_t, perlin8, hw_random16, random16 |
| 210 | PS Chase | mode_particleChase | particle 1D | 1D | no | !,Density,Size,Hue,Blur,Playful,,Position Color | ,! | ! | 1 | pal=11,sx=50,c2=5,c3=0 | 88 | 10202 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | ParticleSystem1D, initParticleSystem1D, sin16_t, hw_random16, random16, strip.now |
| 211 | PS Starburst | mode_particleStarburst | particle 1D | 1D | no | Chance,Fragments,Size,Blur,Cooling,Gravity,Colorful,Push | ,! | ! | 1 | pal=52,sx=150,ix=150,c1=120,c2=0,c3=21 | 51 | 10297 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,call,data | ParticleSystem1D, initParticleSystem1D, hw_random16, hw_random, random16 |
| 212 | PS GEQ 1D | mode_particle1DGEQ | particle 1D | 1D | FFT | Speed,!,Size,Blur,,,, | ,! | ! | 1f | pal=0,sx=50,ix=200,c1=0,c2=0,c3=0,o1=1,o2=1 | 65 | 10355 | speed,intensity,custom1,custom2,call,data | um_data, getAudioData, ParticleSystem1D, initParticleSystem1D, hw_random16, hw_random, random16 |
| 213 | PS Fire 1D | mode_particleFire1D | particle 1D | 1D | no | !,!,Cooling,Blur | ,! | ! | 1 | pal=35,sx=100,ix=50,c1=80,c2=100,c3=28,o1=1,o2=1 | 57 | 10427 | speed,intensity,custom1,custom2,call,data | ParticleSystem1D, initParticleSystem1D, hw_random16, random16 |
| 214 | PS Sonic Stream | mode_particle1DsonicStream | particle 1D | 1D | FFT | !,!,Color,Blur,Bin,Mod,Filter,Push | ,! | ! | 1f | c3=0,o2=1 | 96 | 10490 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | um_data, getAudioData, ParticleSystem1D, initParticleSystem1D, sqrt32_bw, perlin8 |
| 215 | PS Sonic Boom | mode_particle1DsonicBoom | particle 1D | 1D | FFT | !,!,Color,Position,Bin,Mod,Filter,Blur | ,! | ! | 1f | c2=63,c3=0,o2=1 | 83 | 10593 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | um_data, getAudioData, ParticleSystem1D, initParticleSystem1D, sqrt32_bw, perlin8, hw_random16, hw_random, random16 |
| 216 | PS Springy | mode_particleSpringy | particle 1D | 1D | FFT | Stiffness,Damping,Density,Hue,Mode,Smear,XL,AR | ,! | ! | 1f | pal=54,c2=0,c3=23 | 162 | 10682 | speed,intensity,custom1,custom2,custom3,check1,check2,check3,aux0,aux1,step,call,data | um_data, getAudioData, ParticleSystem1D, initParticleSystem1D, sin16_t, hw_random16, random16, strip.now |
| 217 | PS Galaxy | mode_particlegalaxy | particle 2D | 2D | no | !,!,Size,,Color,,Starfield,Trace |  | ! | 2 | pal=59,sx=80,c1=1,c3=4 | 91 | 9309 | speed,intensity,custom1,custom3,check2,check3,call,data | ParticleSystem2D, initParticleSystem2D, sqrt32_bw, hw_random8, hw_random16, random8, random16 |
| 218 | Color Clouds | mode_ColorClouds | 1D | 1D | no | !,!,Clouds,Colors,Distance,,,Cozy |  | ! | (none=1D) | sx=24,ix=32,c1=48,c2=64,c3=12,pal=0 | 64 | 4962 | speed,intensity,custom1,custom2,custom3,check3,aux0,aux1,call | setPixelColor, color_from_palette, SEGLEN, cos8_t, perlin16, hw_random16, random16, CRGB, CHSV, strip.now |
| 219 | Slow Transition | mode_slow_transition | 1D | 1D | no | Time (min),,,,,,Sweep | ! | ! | 1 | pal=2,sx=0,ix=0 | 77 | 10866 | speed,check2,aux0,aux1,step,call,data | setPixelColor, color_blend, ColorFromPalette, allocateData, SEGPALETTE, SEGCOLOR, SEGLEN, CRGB, strip.now |

---

## 7. Surprises and gotchas worth flagging before any porting starts

1. **The mode function signature changed.** `typedef void (*mode_ptr)()` (`FX.h:815`). The old `uint16_t mode_x()` returning a frame delay is gone in 16.x. Every 0.14/0.15-era port guide and every third-party effect written against the old API is wrong for this tag.
2. **`random()` is not Arduino's `random()`.** `#define random hw_random` in `fcn_declare.h:563` routes it to the ESP hardware RNG register. Effects are therefore not reproducible frame to frame, and a host-side or non-ESP port has to supply an equivalent. A separate deterministic `PRNG` class exists in `prng.h` for the six places that need repeatability.
3. **`SEGMENT` and `SEGENV` are the same thing.** Both expand to `(*strip._currentSegment)`. FX.cpp uses them interchangeably, which reads like two different scopes but is not.
4. **`SEGLEN`, `SEG_W`, `SEG_H`, `SEGCOLOR`, `SEGPALETTE` are static class members**, cached once per effect call in `beginDraw()`. They are not per-segment reads. An ESPHome port that makes them instance lookups will lose real performance in the pixel loops.
5. **The `um_data` doc comment in FX.cpp is wrong in two places** (lines 47 to 66): it types `u_data[1]` as `float` when both producers store `uint16_t`, and it documents a `u_data[8]` (`fftBin`) that neither the usermod nor `simulateSound()` allocates. `u_size` is 8, so index 8 is out of bounds.
6. **`u_data[6]` and `u_data[7]` are written *by* effects**, not just read. Puddlepeak, Ripple Peak and Waterfall push `maxVol` and `binNum` back into the shared struct from their UI sliders. A read-only audio provider will break them.
7. **WLED 16 does not depend on FastLED.** It vendors a 1,060 line MIT subset and replaced the rest with its own EUPL math. Do not plan the port around linking FastLED; do plan for the licence split.
8. **One malformed metadata string.** `_data_FX_MODE_FLOWSTRIPE` (`FX.cpp:5082`) is `"Flow Stripe@Hue speed,Effect speed;;!;pal=11"`, only four groups. `extractModeDefaults()` reads the last group and correctly picks up `pal=11`, but the web UI reads group index 3 as the flags field, so it sees flags `"pal=11"`, which contains a `1` and a `2`. Flow Stripe is therefore advertised as both 1D and 2D capable purely by accident. Four other effects (`FIREWORKS`, `2DBLACKHOLE`, `2DDRIFTROSE`, `2DSOAP`) use the same `pal=11` default correctly, with five groups.
9. **Four reserved slots.** IDs 142 (`FX_MODE_BINMAP`, defined in FX.h but never registered) and 169, 170, 171 (`2DPOOLNOISE`, `2DTWISTER`, `2DCAELEMENTATY`, all commented out in FX.h with a note that they were dropped for memory and should come back). `setupEffectData()` pre-fills all 220 slots with `mode_static` and `_data_RESERVED`, so the gaps render as Solid rather than crashing. Preserve the numbering; effect IDs are persisted in presets and the JSON API.
10. **Eight effects are both particle and audio** (IDs 197, 198, 199, 201, 212, 214, 215, 216). Any port plan that treats "particle effects" and "audio effects" as disjoint work packages will double count or drop these.
11. **The particle system borrows the segment's pixel buffer** rather than owning one (`framebuffer = SEGMENT.getPixels()`), and is a `friend class` of `Segment` so it can reach protected raw accessors. The encapsulation boundary is deliberately broken for speed.
12. **Particle memory is inside the segment data budget**, which is only 6 KB on ESP8266 and 20 KB on ESP32-S2. `initParticleSystem2D()` degrades by halving the particle count until allocation succeeds, down to a floor of 5, and ESP8266 cannot compile both particle systems at once.
13. **Palette gamma is baked in.** cpt-city palettes are pre-corrected with (1.182, 1.0, 1.136) and FastLED palettes get an inverse 2.2, both on the assumption of a global 2.2 output gamma. An ESPHome port with different gamma handling will not match WLED's colors even with identical palette data.
14. **Effect defaults are data, not code.** `Segment::setMode(fx, loadDefaults=true)` re-reads the metadata string and applies `sx=`, `ix=`, `c1=`, `pal=`, `m12=`, `si=` and friends. A port that hardcodes defaults in the effect body will drift from upstream on every WLED release.
15. **`WLED_DISABLE_2D` already gives you a clean 1D-only build.** Every `*XY` method has an inline 1D shim in `FX.h` lines 777 to 810. That is the cheapest path to a first ESPHome milestone.
