/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Original: Harm Aldick 2016, www.aldick.org. Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

#include "wf_effects.h"

// Whole-file guard: with an allow-list that selects nothing from this group, the
// translation unit compiles to nothing at all.
#define WLED_FX_GROUP_1D_A                                                                           \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID || WLED_FX_FX_BLINK || WLED_FX_FX_RAINBOW ||           \
   WLED_FX_FX_PRIDE_2015 || WLED_FX_FX_FIRE_2012 || WLED_FX_FX_NOISE_1 || WLED_FX_FX_PLASMA)

#if WLED_FX_GROUP_1D_A

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID
/*
 * No blinking. Just plain old static light.
 */
void mode_static(Segment &seg) { seg.fill(seg.color(0)); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLINK
/*
 * Blink/strobe function
 * Alternate between color1 and color2
 * if(strobe == true) then create a strobe effect
 */
void blink(Segment &seg, uint32_t color1, uint32_t color2, bool strobe, bool do_palette) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = (255 - seg.speed) * 20;
  uint32_t onTime = FRAMETIME;
  if (!strobe)
    onTime += ((cycleTime * seg.intensity) >> 8);
  cycleTime += FRAMETIME * 2;
  uint32_t it = seg.now / cycleTime;
  uint32_t rem = seg.now % cycleTime;

  bool on = false;
  if (it != seg.step  // new iteration, force on state for one frame, even if set time is too brief
      || rem <= onTime) {
    on = true;
  }

  seg.step = it;  // save previous iteration

  uint32_t color = on ? color1 : color2;
  if (color == color1 && do_palette) {
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
    }
  } else {
    seg.fill(color);
  }
}

/*
 * Normal blinking. Intensity sets duty cycle.
 */
void mode_blink(Segment &seg) { blink(seg, seg.color(0), seg.color(1), false, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAINBOW
/*
 * Cycles a rainbow over the entire string of LEDs.
 */
void mode_rainbow_cycle(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned counter = (seg.now * ((seg.speed >> 2) + 2)) & 0xFFFF;
  counter = counter >> 8;

  for (unsigned i = 0; i < seg_len; i++) {
    // intensity/29 = 0 (1/16) 1 (1/8) 2 (1/4) 3 (1/2) 4 (1) 5 (2) 6 (4) 7 (8) 8 (16)
    uint8_t index = (i * (16 << (seg.intensity / 29)) / seg_len) + counter;
    seg.set_pixel_color(i, seg.color_wheel(index));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PRIDE_2015
// combined function from original pride and colorwaves
void mode_colorwaves_pride_base(Segment &seg, bool isPride2015) {
  const unsigned seg_len = seg.length();
  unsigned duration = 10 + seg.speed;
  unsigned sPseudotime = seg.step;
  unsigned sHue16 = seg.aux0;

  uint8_t sat8 = isPride2015 ? beatsin88_t(87, 220, 250, seg.now) : 255;
  unsigned brightdepth = beatsin88_t(341, 96, 224, seg.now);
  unsigned brightnessthetainc16 = beatsin88_t(203, (25 * 256), (40 * 256), seg.now);
  unsigned msmultiplier = beatsin88_t(147, 23, 60, seg.now);

  unsigned hue16 = sHue16;
  unsigned hueinc16 =
      isPride2015 ? beatsin88_t(113, 1, 3000, seg.now) : beatsin88_t(113, 60, 300, seg.now) * seg.intensity * 10 / 255;

  sPseudotime += duration * msmultiplier;
  sHue16 += duration * beatsin88_t(400, 5, 9, seg.now);
  unsigned brightnesstheta16 = sPseudotime;

  for (unsigned i = 0; i < seg_len; i++) {
    hue16 += hueinc16;
    uint8_t hue8;

    if (isPride2015) {
      hue8 = hue16 >> 8;
    } else {
      unsigned h16_128 = hue16 >> 7;
      hue8 = (h16_128 & 0x100) ? (255 - (h16_128 >> 1)) : (h16_128 >> 1);
    }

    brightnesstheta16 += brightnessthetainc16;
    unsigned b16 = sin16_t(brightnesstheta16) + 32768;
    unsigned bri16 = (uint32_t) ((uint32_t) b16 * (uint32_t) b16) / 65536;
    uint8_t bri8 = (uint32_t) (((uint32_t) bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    if (isPride2015) {
      CRGBW newcolor = CRGB(CHSV(hue8, sat8, bri8));
      newcolor.color32 = gamma32inv(newcolor.color32);
      seg.blend_pixel_color(i, newcolor, 64);
    } else {
      seg.blend_pixel_color(i, seg.color_from_palette(hue8, false, seg.palette_solid_wrap(), 0, bri8), 128);
    }
  }

  seg.step = sPseudotime;
  seg.aux0 = sHue16;
}

// Pride2015
// Animated, ever-changing rainbows.
// by Mark Kriegsman: https://gist.github.com/kriegsman/964de772d64c502760e5
void mode_pride_2015(Segment &seg) { mode_colorwaves_pride_base(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRE_2012
// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (Speed slider) and SPARKING (Effect Intensity).
void mode_fire_2012(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const unsigned strips = seg.nr_of_v_strips();
  if (!seg.allocate_data(strips * seg_len))
    FX_FALLBACK_STATIC;  // allocation failed
  uint8_t *heat = seg.data;

  const uint32_t it = seg.now >> 5;  // div 32

  const auto run_strip = [&](unsigned stripNr, uint8_t *heat, uint32_t it) {
    const uint8_t ignition = seg_len / 10 > 3 ? seg_len / 10 : 3;  // 10% of length or at least 3 pixels

    // Step 1.  Cool down every cell a little
    for (unsigned i = 0; i < seg_len; i++) {
      uint8_t cool = (it != seg.step) ? hw_random8((((20 + seg.speed / 3) * 16) / seg_len) + 2) : hw_random8(4);
      uint8_t minTemp = (i < ignition) ? (ignition - i) / 4 + 16 : 0;  // should not become black in ignition area
      uint8_t temp = qsub8(heat[i], cool);
      heat[i] = temp < minTemp ? minTemp : temp;
    }

    if (it != seg.step) {
      // Step 2.  Heat from each cell drifts 'up' and diffuses a little
      for (int k = seg_len - 1; k > 1; k--) {
        heat[k] = (heat[k - 1] + (heat[k - 2] << 1)) / 3;  // heat[k-2] multiplied by 2
      }

      // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
      if (hw_random8() <= seg.intensity) {
        uint8_t y = hw_random8(ignition);
        uint8_t boost = (17 + seg.custom3) * (ignition - y / 2) / ignition;  // integer math!
        heat[y] = qadd8(heat[y], hw_random8(96 + 2 * boost, 207 + boost));
      }
    }

    // Step 4.  Map from heat cells to LED colors
    for (unsigned j = 0; j < seg_len; j++) {
      seg.set_pixel_color(Segment::index_to_v_strip(j, stripNr),
                          ColorFromPalette(seg.palette_ref(), heat[j] < 240 ? heat[j] : 240, 255, NOBLEND));
    }
  };

  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &heat[stripNr * seg_len], it);

  if (seg.is_2d()) {
    uint8_t blurAmount = seg.custom2 >> 2;
    if (blurAmount > 48)
      blurAmount += blurAmount - 48;  // extra blur when slider > 192  (bush burn)
    if (blurAmount < 16)
      seg.blur_cols(seg.custom2 >> 1);  // no side-burn when slider < 64 (faster)
    else
      seg.blur(blurAmount);
  }

  if (it != seg.step)
    seg.step = it;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_1
void mode_noise16_1(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned scale = 320;  // the "zoom factor" for the noise
  seg.step += (1 + seg.speed / 16);

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned shift_x = beatsin8_t(11, 0, 255, seg.now);       // the x position of the noise field swings @ 17 bpm
    unsigned shift_y = seg.step / 42;                         // the y position becomes slowly incremented
    unsigned real_x = (i + shift_x) * scale;                  // the x position of the noise field
    unsigned real_y = (i + shift_y) * scale;                  // the y position becomes slowly incremented
    uint32_t real_z = seg.step;                               // the z position becomes quickly incremented
    unsigned noise = perlin16(real_x, real_y, real_z) >> 8;   // get the noise data and scale it down
    unsigned index = sin8_t(noise * 3);                       // map LED color based on noise data

    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMA
/*
 * Plasma Effect
 * adapted from https://github.com/atuline/FastLED-Demos/blob/master/plasma/plasma.ino
 */
void mode_plasma(Segment &seg) {
  const unsigned seg_len = seg.length();
  // initialize phases on start
  if (seg.call == 0) {
    seg.aux0 = hw_random8(0, 2);  // add a bit of randomness
  }
  unsigned thisPhase = beatsin8_t(6 + seg.aux0, -64, 64, seg.now);
  unsigned thatPhase = beatsin8_t(7 + seg.aux0, -64, 64, seg.now);

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned colorIndex = cubicwave8((i * (2 + 3 * (seg.speed >> 5)) + thisPhase) & 0xFF) / 2 +
                          cos8_t((i * (1 + 2 * (seg.speed >> 5)) + thatPhase) & 0xFF) / 2;
    unsigned thisBright = qsub8(colorIndex, beatsin8_t(7, 0, (128 - (seg.intensity >> 1)), seg.now));
    seg.set_pixel_color(i, seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0, thisBright));
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID
    {"Solid", mode_static},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLINK
    {"Blink@!,Duty cycle;!,!;!;01", mode_blink},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAINBOW
    {"Rainbow@!,Size;;!", mode_rainbow_cycle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PRIDE_2015
    {"Pride 2015@!;;", mode_pride_2015},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRE_2012
    {"Fire 2012@Cooling,Spark rate,,2D Blur,Boost;;!;1;pal=35,sx=64,ix=160,m12=1,c2=128", mode_fire_2012},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_1
    {"Noise 1@!;!;!;;pal=20", mode_noise16_1},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMA
    {"Plasma@Phase,!;!;!", mode_plasma},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table below would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_A;
const EffectGroup EFFECT_GROUP_1D_A{"1d_a", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_A
