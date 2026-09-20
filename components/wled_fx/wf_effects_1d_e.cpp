/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

/* 1D batch E. See BATCHES.md for the effect list this file owns. */

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_1D_E                                                                                 \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE || WLED_FX_FX_CHASE_RANDOM || WLED_FX_FX_CHASE_RAINBOW ||    \
   WLED_FX_FX_RAINBOW_RUNNER || WLED_FX_FX_CHASE_3 || WLED_FX_FX_CANDLE || WLED_FX_FX_CANDLE_MULTI ||      \
   WLED_FX_FX_PHASED || WLED_FX_FX_PHASED_NOISE || WLED_FX_FX_COLORWAVES || WLED_FX_FX_FIREWORKS_STARBURST || \
   WLED_FX_FX_BOUNCING_BALLS || WLED_FX_FX_SLOW_TRANSITION || WLED_FX_FX_COLOR_CLOUDS || WLED_FX_FX_PERCENT || \
   WLED_FX_FX_LIGHTNING || WLED_FX_FX_ANDROID || WLED_FX_FX_MULTI_COMET || WLED_FX_FX_HEARTBEAT ||         \
   WLED_FX_FX_LIGHTHOUSE || WLED_FX_FX_SOLID_PATTERN || WLED_FX_FX_STREAM_2 || WLED_FX_FX_LAKE ||          \
   WLED_FX_FX_BPM)

#if WLED_FX_GROUP_1D_E

// Guards for the upstream helpers this file has to carry, so a narrow allow-list
// does not drag in a helper nothing calls.
#define WLED_FX_1D_E_CHASE_BASE                                                                          \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE || WLED_FX_FX_CHASE_RANDOM || WLED_FX_FX_CHASE_RAINBOW ||  \
   WLED_FX_FX_RAINBOW_RUNNER)
#define WLED_FX_1D_E_CANDLE_BASE (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CANDLE || WLED_FX_FX_CANDLE_MULTI)
#define WLED_FX_1D_E_PHASED_BASE (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PHASED || WLED_FX_FX_PHASED_NOISE)

namespace esphome {
namespace wled_fx {
namespace {

/* The shared PRNG, get_random_wheel_index(), NUM_COLORS, FRAMETIME_FIXED,
 * FAIR_DATA_PER_SEG and mode_colorwaves_pride_base() are engine code now, in
 * wf_fx_shared.h and wf_segment.h. */
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ANDROID
/*
 * Android loading circle, refactored by @dedehai
 */
void mode_android(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.allocate_data(sizeof(uint32_t)))
    FX_FALLBACK_STATIC;
  uint32_t *counter = reinterpret_cast<uint32_t *>(seg.data);
  unsigned size = seg.aux1 >> 1;      // upper 15 bit
  unsigned shrinking = seg.aux1 & 0x01;  // lowest bit
  if (seg.now >= seg.step) {
    seg.step = seg.now + 3 + ((8 * (uint32_t) (255 - seg.speed)) / seg_len);
    if (size > (seg.intensity * seg_len) / 255)
      shrinking = 1;
    else if (size < 2)
      shrinking = 0;
    if (!shrinking) {  // growing
      if ((*counter % 3) == 1)
        seg.aux0++;  // advance start position
      else
        size++;
    } else {  // shrinking
      seg.aux0++;
      if ((*counter % 3) != 1)
        size--;
    }
    seg.aux1 = size << 1 | shrinking;  // save back
    (*counter)++;
    if (seg.aux0 >= seg_len)
      seg.aux0 = 0;
  }
  uint32_t start = seg.aux0;
  uint32_t end = (seg.aux0 + size) % seg_len;
  for (unsigned i = 0; i < seg_len; i++) {
    if ((start < end && i >= start && i < end) || (start >= end && (i >= start || i < end)))
      seg.set_pixel_color(i, seg.color(0));
    else
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1));
  }
}
#endif

#if WLED_FX_1D_E_CHASE_BASE
/*
 * color chase function.
 * color1 = background color
 * color2 and color3 = colors of two adjacent leds
 *
 * Upstream reads SEGMENT.mode to tell "Chase Random" apart from its siblings.
 * This port has no segment mode field, so the caller passes the flag instead.
 */
void chase(Segment &seg, uint32_t color1, uint32_t color2, uint32_t color3, bool do_palette, bool chase_random) {
  const unsigned seg_len = seg.length();
  uint16_t counter = seg.now * ((seg.speed >> 2) + 1);
  uint16_t a = (counter * seg_len) >> 16;

  if (chase_random) {
    if (a < seg.step)  // we hit the start again, choose new color for Chase random
    {
      seg.aux1 = seg.aux0;  // store previous random color
      seg.aux0 = get_random_wheel_index(seg.aux0);
    }
    color1 = seg.color_wheel(seg.aux0);
  }
  seg.step = a;

  // Use intensity setting to vary chase up to 1/2 string length
  unsigned size = 1 + ((seg.intensity * seg_len) >> 10);

  uint16_t b = a + size;  //"trail" of chase, filled with color1
  if (b > seg_len)
    b -= seg_len;
  uint16_t c = b + size;
  if (c > seg_len)
    c -= seg_len;

  // background
  if (do_palette) {
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1));
    }
  } else
    seg.fill(color1);

  // if random, fill old background between a and end
  if (chase_random) {
    color1 = seg.color_wheel(seg.aux1);
    for (unsigned i = a; i < seg_len; i++)
      seg.set_pixel_color(i, color1);
  }

  // fill between points a and b with color2
  if (a < b) {
    for (unsigned i = a; i < b; i++)
      seg.set_pixel_color(i, color2);
  } else {
    for (unsigned i = a; i < seg_len; i++)  // fill until end
      seg.set_pixel_color(i, color2);
    for (unsigned i = 0; i < b; i++)  // fill from start until b
      seg.set_pixel_color(i, color2);
  }

  // fill between points b and c with color2
  if (b < c) {
    for (unsigned i = b; i < c; i++)
      seg.set_pixel_color(i, color3);
  } else {
    for (unsigned i = b; i < seg_len; i++)  // fill until end
      seg.set_pixel_color(i, color3);
    for (unsigned i = 0; i < c; i++)  // fill from start until c
      seg.set_pixel_color(i, color3);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE
/*
 * Bicolor chase, more primary color.
 */
void mode_chase_color(Segment &seg) {
  chase(seg, seg.color(1), (seg.color(2)) ? seg.color(2) : seg.color(0), seg.color(0), true, false);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_RANDOM
/*
 * Primary running followed by random color.
 */
void mode_chase_random(Segment &seg) {
  chase(seg, seg.color(1), (seg.color(2)) ? seg.color(2) : seg.color(0), seg.color(0), false, true);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_RAINBOW
/*
 * Primary, secondary running on rainbow.
 */
void mode_chase_rainbow(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned color_sep = 256 / seg_len;
  if (color_sep == 0)
    color_sep = 1;  // correction for segments longer than 256 LEDs
  unsigned color_index = seg.call & 0xFF;
  uint32_t color = seg.color_wheel(((seg.step * color_sep) + color_index) & 0xFF);

  chase(seg, color, seg.color(0), seg.color(1), false, false);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAINBOW_RUNNER
/*
 * Primary running on rainbow.
 */
void mode_chase_rainbow_white(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint16_t n = seg.step;
  uint16_t m = (seg.step + 1) % seg_len;
  uint32_t color2 = seg.color_wheel(((n * 256 / seg_len) + (seg.call & 0xFF)) & 0xFF);
  uint32_t color3 = seg.color_wheel(((m * 256 / seg_len) + (seg.call & 0xFF)) & 0xFF);

  chase(seg, seg.color(0), color2, color3, false, false);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LIGHTHOUSE
/*
 * Firing comets from one end. "Lighthouse"
 */
void mode_comet(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned counter = (seg.now * ((seg.speed >> 2) + 1)) & 0xFFFF;
  unsigned index = (counter * seg_len) >> 16;
  if (seg.call == 0)
    seg.aux0 = index;

  seg.fade_out(seg.intensity);

  seg.set_pixel_color(index, seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0));
  if (index > seg.aux0) {
    for (unsigned i = seg.aux0; i < index; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
    }
  } else if (index < seg.aux0 && index < 10) {
    for (unsigned i = 0; i < index; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
    }
  }
  seg.aux0 = index++;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_3
/*
 * Tricolor chase function
 */
void tricolor_chase(Segment &seg, uint32_t color1, uint32_t color2) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = 50 + ((255 - seg.speed) << 1);
  uint32_t it = seg.now / cycleTime;         // iterator
  unsigned width = (1 + (seg.intensity >> 4));  // value of 1-16 for each colour
  unsigned index = it % (width * 3);

  for (unsigned i = 0; i < seg_len; i++, index++) {
    if (index > (width * 3) - 1)
      index = 0;

    uint32_t color = color1;
    if (index > (width << 1) - 1)
      color = seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1);
    else if (index > width - 1)
      color = color2;

    seg.set_pixel_color(seg_len - i - 1, color);
  }
}

/*
 * Tricolor chase mode
 */
void mode_tricolor_chase(Segment &seg) { tricolor_chase(seg, seg.color(2), seg.color(0)); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MULTI_COMET
/*
 * Multi Comet
 * Custom mode by Keith Lord: https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/MultiComet.h
 */
#define MAX_COMETS 8
void mode_multi_comet(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = 10 + (uint32_t) (255 - seg.speed);
  uint32_t it = seg.now / cycleTime;
  if (seg.step == it)
    return;
  if (!seg.allocate_data(sizeof(uint16_t) * MAX_COMETS))
    FX_FALLBACK_STATIC;  // allocation failed

  seg.fade_out(seg.intensity / 2 + 128);

  uint16_t *comets = reinterpret_cast<uint16_t *>(seg.data);

  for (unsigned i = 0; i < MAX_COMETS; i++) {
    if (comets[i] < seg_len) {
      unsigned index = comets[i];
      if (seg.color(2) != 0) {
        seg.set_pixel_color(index,
                            i % 2 ? seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0) : seg.color(2));
      } else {
        seg.set_pixel_color(index, seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0));
      }
      comets[i]++;
    } else {
      if (!hw_random16(seg_len)) {
        comets[i] = 0;
      }
    }
  }

  seg.step = it;
}
#undef MAX_COMETS
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STREAM_2
/*
 * Running random pixels ("Stream 2")
 * Custom mode by Keith Lord: https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/RandomChase.h
 */
void mode_random_chase(Segment &seg) {
  const unsigned seg_len = seg.length();
  Prng &prng = fx_prng();
  if (seg.call == 0) {
    seg.step = RGBW32(prng.random8(), prng.random8(), prng.random8(), 0);
    seg.aux0 = prng.random16();
  }
  unsigned prevSeed = prng.get_seed();  // save seed so we can restore it at the end of the function
  uint32_t cycleTime = 25 + (3 * (uint32_t) (255 - seg.speed));
  uint32_t it = seg.now / cycleTime;
  uint32_t color = seg.step;
  prng.set_seed(seg.aux0);

  for (int i = (int) seg_len - 1; i >= 0; i--) {
    uint8_t r = prng.random8(6) != 0 ? (color >> 16 & 0xFF) : prng.random8();
    uint8_t g = prng.random8(6) != 0 ? (color >> 8 & 0xFF) : prng.random8();
    uint8_t b = prng.random8(6) != 0 ? (color & 0xFF) : prng.random8();
    color = RGBW32(r, g, b, 0);
    seg.set_pixel_color(i, color);
    if ((unsigned) i == seg_len - 1U && seg.aux1 != (it & 0xFFFFU)) {  // new first color in next frame
      seg.step = color;
      seg.aux0 = prng.get_seed();
    }
  }

  seg.aux1 = it & 0xFFFF;

  prng.set_seed(prevSeed);  // restore original seed so other effects can use "random" PRNG
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LIGHTNING
void mode_lightning(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned ledstart = hw_random16(seg_len);                // Determine starting location of flash
  unsigned ledlen = 1 + hw_random16(seg_len - ledstart);   // Determine length of flash (not to go beyond NUM_LEDS-1)
  uint8_t bri = 255 / hw_random8(1, 3);

  if (seg.aux1 == 0)  // init, leader flash
  {
    seg.aux1 = hw_random8(4, 4 + seg.intensity / 20);  // number of flashes
    seg.aux1 *= 2;

    bri = 52;         // leader has lower brightness
    seg.aux0 = 200;   // 200ms delay after leader
  }

  if (!seg.check2)
    seg.fill(seg.color(1));

  if (seg.aux1 > 3 && !(seg.aux1 & 0x01)) {  // flash on even number >2
    for (unsigned i = ledstart; i < ledstart + ledlen; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0, bri));
    }
    seg.aux1--;

    seg.step = seg.now;
    // return hw_random8(4, 10); // each flash only lasts one frame/every 24ms... originally 4-10 milliseconds
  } else {
    if (seg.now - seg.step > seg.aux0) {
      seg.aux1--;
      if (seg.aux1 < 2)
        seg.aux1 = 0;

      seg.aux0 = (50 + hw_random8(100));  // delay between flashes
      if (seg.aux1 == 2) {
        seg.aux0 = (hw_random8(255 - seg.speed) * 100);  // delay between strikes
      }
      seg.step = seg.now;
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORWAVES
// ColorWavesWithPalettes by Mark Kriegsman: https://gist.github.com/kriegsman/8281905786e8b2632aeb
// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void mode_colorwaves(Segment &seg) { mode_colorwaves_pride_base(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BPM
// colored stripes pulsing at a defined Beats-Per-Minute (BPM)
void mode_bpm(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t stp = (seg.now / 20) & 0xFF;
  uint8_t beat = beatsin8_t(seg.speed, 64, 255, seg.now);
  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, seg.color_from_palette(stp + (i * 2), false, seg.palette_solid_wrap(), 0,
                                                  beat - stp + (i * 10)));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LAKE
// Calm effect, like a lake at night
void mode_lake(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned sp = seg.speed / 10;
  int wave1 = beatsin8_t(sp + 2, -64, 64, seg.now);
  int wave2 = beatsin8_t(sp + 1, -64, 64, seg.now);
  int wave3 = beatsin8_t(sp + 2, 0, 80, seg.now);

  for (unsigned i = 0; i < seg_len; i++) {
    int index = cos8_t((i * 15) + wave1) / 2 + cubicwave8((i * 23) + wave2) / 2;
    uint8_t lum = (index > wave3) ? index - wave3 : 0;
    seg.set_pixel_color(i, seg.color_from_palette(index, false, false, 0, lum));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_PATTERN
// Speed slider sets amount of LEDs lit, intensity sets unlit
void mode_static_pattern(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned lit = 1 + seg.speed;
  unsigned unlit = 1 + seg.intensity;
  bool drawingLit = true;
  unsigned cnt = 0;

  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, (drawingLit) ? seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0) : seg.color(1));
    cnt++;
    if (cnt >= ((drawingLit) ? lit : unlit)) {
      cnt = 0;
      drawingLit = !drawingLit;
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BOUNCING_BALLS
// each needs 12 bytes
typedef struct Ball {
  unsigned long lastBounceTime;
  float impactVelocity;
  float height;
} ball;

/*
 *  Bouncing Balls Effect
 */
void mode_bouncing_balls(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // allocate segment data
  const unsigned strips = seg.nr_of_v_strips();  // adapt for 2D
  const size_t maxNumBalls = 16;
  unsigned dataSize = sizeof(ball) * maxNumBalls;
  if (!seg.allocate_data(dataSize * strips))
    FX_FALLBACK_STATIC;  // allocation failed

  Ball *balls = reinterpret_cast<Ball *>(seg.data);

  if (!seg.check2)
    seg.fill(seg.color(2) ? BLACK : seg.color(1));

  // virtualStrip idea by @ewowi (Ewoud Wijma)
  // requires virtual strip # to be embedded into upper 16 bits of index in setPixelColor()
  // the following functions will not work on virtual strips: fill(), fade_out(), fadeToBlack(), blur()
  const auto run_strip = [&](size_t stripNr, Ball *balls) {
    // number of balls based on intensity setting to max of 7 (cycles colors)
    // non-chosen color is a random color
    unsigned numBalls = (seg.intensity * (maxNumBalls - 1)) / 255 + 1;  // minimum 1 ball
    const float gravity = -9.81f;                                       // standard value of gravity
    const bool hasCol2 = seg.color(2);
    const unsigned long time = seg.now;

    if (seg.call == 0) {
      for (size_t i = 0; i < maxNumBalls; i++)
        balls[i].lastBounceTime = time;
    }

    for (size_t i = 0; i < numBalls; i++) {
      float timeSinceLastBounce = (time - balls[i].lastBounceTime) / ((255 - seg.speed) / 64 + 1);
      float timeSec = timeSinceLastBounce / 1000.0f;
      balls[i].height =
          (0.5f * gravity * timeSec + balls[i].impactVelocity) * timeSec;  // avoid use pow(x, 2) - its extremely slow !

      if (balls[i].height <= 0.0f) {
        balls[i].height = 0.0f;
        // damping for better effect using multiple balls
        float dampening = 0.9f - float(i) / float(numBalls * numBalls);  // avoid use pow(x, 2) - its extremely slow !
        balls[i].impactVelocity = dampening * balls[i].impactVelocity;
        balls[i].lastBounceTime = time;

        if (balls[i].impactVelocity < 0.015f) {
          float impactVelocityStart = sqrtf(-2.0f * gravity) * hw_random8(5, 11) / 10.0f;  // randomize impact velocity
          balls[i].impactVelocity = impactVelocityStart;
        }
      } else if (balls[i].height > 1.0f) {
        continue;  // do not draw OOB ball
      }

      uint32_t color = seg.color(0);
      if (seg.palette) {
        color = seg.color_wheel(i * (256 / (numBalls > 8 ? numBalls : 8)));  // MAX(numBalls, 8)
      } else if (hasCol2) {
        color = seg.color(i % NUM_COLORS);
      }

      int pos = roundf(balls[i].height * (seg_len - 1));
      seg.set_pixel_color(Segment::index_to_v_strip(pos, stripNr), color);  // encode virtual strip into index
    }
  };

  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &balls[stripNr * maxNumBalls]);
}
#endif

#if WLED_FX_1D_E_CANDLE_BASE
// values close to 100 produce 5Hz flicker, which looks very candle-y
// Inspired by https://github.com/avanhanegem/ArduinoCandleEffectNeoPixel
// and https://cpldcpu.wordpress.com/2016/01/05/reverse-engineering-a-real-candle/

void candle(Segment &seg, bool multi) {
  const unsigned seg_len = seg.length();
  if (multi && seg_len > 1) {
    // allocate segment data
    unsigned dataSize = sizeof(uint32_t) + (1 > (int) seg_len - 1 ? 1 : (int) seg_len - 1) * 3;
    if (!seg.allocate_data(dataSize)) {
      /* Upstream falls back to the single candle here and then carries on into
       * the multi-candle body. It gets away with it because the recursive call
       * stamps *lastcall, so the rate limit below returns before anything is
       * written. It does not get away with it when the 4 byte allocation the
       * recursive call makes fails as well: seg.data is then null and the read
       * of *lastcall below dereferences it. One return, and the outcome is the
       * same in every case upstream survives. */
      candle(seg, false);  // allocation failed
      return;
    }
  } else {
    unsigned dataSize = sizeof(uint32_t);  // for last call timestamp
    if (!seg.allocate_data(dataSize))
      FX_FALLBACK_STATIC;  // allocation failed
  }
  uint32_t *lastcall = reinterpret_cast<uint32_t *>(seg.data);
  uint8_t *candleData = reinterpret_cast<uint8_t *>(seg.data + sizeof(uint32_t));  // only used for multi-candle

  // limit update rate
  if (seg.now - *lastcall < FRAMETIME_FIXED)
    return;
  *lastcall = seg.now;

  // max. flicker range controlled by intensity
  unsigned valrange = seg.intensity;
  unsigned rndval = valrange >> 1;  // max 127

  // step (how much to move closer to target per frame) coarsely set by speed
  unsigned speedFactor = 4;
  if (seg.speed > 252) {  // epilepsy
    speedFactor = 1;
  } else if (seg.speed > 99) {  // regular candle (mode called every ~25 ms, so 4 frames to have a new target every 100ms)
    speedFactor = 2;
  } else if (seg.speed > 49) {  // slower fade
    speedFactor = 3;
  }  // else 4 (slowest)

  unsigned numCandles = (multi) ? seg_len : 1;

  for (unsigned i = 0; i < numCandles; i++) {
    unsigned d = 0;  // data location

    unsigned s = seg.aux0, s_target = seg.aux1, fadeStep = seg.step;
    if (i > 0) {
      d = (i - 1) * 3;
      s = candleData[d];
      s_target = candleData[d + 1];
      fadeStep = candleData[d + 2];
    }
    if (fadeStep == 0) {  // init vals
      s = 128;
      s_target = 130 + hw_random8(4);
      fadeStep = 1;
    }

    bool newTarget = false;
    if (s_target > s) {  // fade up
      s = qadd8(s, fadeStep);
      if (s >= s_target)
        newTarget = true;
    } else {
      s = qsub8(s, fadeStep);
      if (s <= s_target)
        newTarget = true;
    }

    if (newTarget) {
      s_target = hw_random8(rndval) + hw_random8(rndval);  // between 0 and rndval*2 -2 = 252
      if (s_target < (rndval >> 1))
        s_target = (rndval >> 1) + hw_random8(rndval);
      unsigned offset = (255 - valrange);
      s_target += offset;

      unsigned dif = (s_target > s) ? s_target - s : s - s_target;

      fadeStep = dif >> speedFactor;
      if (fadeStep == 0)
        fadeStep = 1;
    }

    if (i > 0) {
      seg.set_pixel_color(
          i, color_blend(seg.color(1), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0), uint8_t(s)));
      candleData[d] = s;
      candleData[d + 1] = s_target;
      candleData[d + 2] = fadeStep;
    } else {
      for (unsigned j = 0; j < seg_len; j++) {
        seg.set_pixel_color(
            j, color_blend(seg.color(1), seg.color_from_palette(j, true, seg.palette_solid_wrap(), 0), uint8_t(s)));
      }

      seg.aux0 = s;
      seg.aux1 = s_target;
      seg.step = fadeStep;
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CANDLE
void mode_candle(Segment &seg) { candle(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CANDLE_MULTI
void mode_candle_multi(Segment &seg) { candle(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_STARBURST
/*
/ Fireworks Starburst
/ Speed sets frequency of new starbursts, intensity is the intensity of the burst
*/
#ifdef ESP8266
#define STARBURST_MAX_FRAG 8  // 52 bytes / star
#else
#define STARBURST_MAX_FRAG 10  // 60 bytes / star
#endif
// each needs 20+STARBURST_MAX_FRAG*4 bytes
typedef struct particle {
  CRGB color;
  uint32_t birth = 0;
  uint32_t last = 0;
  float vel = 0;
  uint16_t pos = -1;
  float fragment[STARBURST_MAX_FRAG];
} star;

void mode_starburst(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned maxData = FAIR_DATA_PER_SEG;  // ESP8266: 256 ESP32: 640
  // One canvas, one segment: getActiveSegmentsNum() and getMaxSegments() are both 1.
  unsigned segs = 1;
  if (segs <= (1 / 2))
    maxData *= 2;  // ESP8266: 512 if <= 8 segs ESP32: 1280 if <= 16 segs
  if (segs <= (1 / 4))
    maxData *= 2;  // ESP8266: 1024 if <= 4 segs ESP32: 2560 if <= 8 segs
  unsigned maxStars = maxData / sizeof(star);  // ESP8266: max. 4/9/19 stars/seg, ESP32: max. 10/21/42 stars/seg

  unsigned numStars = 1 + (seg_len >> 3);
  if (numStars > maxStars)
    numStars = maxStars;
  unsigned dataSize = sizeof(star) * numStars;

  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  uint32_t it = seg.now;

  star *stars = reinterpret_cast<star *>(seg.data);

  float maxSpeed = 375.0f;           // Max velocity
  float particleIgnition = 250.0f;   // How long to "flash"
  float particleFadeTime = 1500.0f;  // Fade out time

  for (unsigned j = 0; j < numStars; j++) {
    // speed to adjust chance of a burst, max is nearly always.
    if (hw_random8((144 - (seg.speed >> 1))) == 0 && stars[j].birth == 0) {
      // Pick a random color and location.
      unsigned startPos = hw_random16(seg_len - 1);
      float multiplier = (float) (hw_random8()) / 255.0f * 1.0f;

      stars[j].color = CRGB(seg.color_wheel(hw_random8()));
      stars[j].pos = startPos;
      stars[j].vel = maxSpeed * (float) (hw_random8()) / 255.0f * multiplier;
      stars[j].birth = it;
      stars[j].last = it;
      // more fragments means larger burst effect
      int num = hw_random8(3, 6 + (seg.intensity >> 5));

      for (int i = 0; i < STARBURST_MAX_FRAG; i++) {
        if (i < num)
          stars[j].fragment[i] = startPos;
        else
          stars[j].fragment[i] = -1;
      }
    }
  }

  if (!seg.check2)
    seg.fill(seg.color(1));

  for (unsigned j = 0; j < numStars; j++) {
    if (stars[j].birth != 0) {
      float dt = (it - stars[j].last) / 1000.0;

      for (int i = 0; i < STARBURST_MAX_FRAG; i++) {
        int var = i >> 1;

        if (stars[j].fragment[i] > 0) {
          // all fragments travel right, will be mirrored on other side
          stars[j].fragment[i] += stars[j].vel * dt * (float) var / 3.0;
        }
      }
      stars[j].last = it;
      stars[j].vel -= 3 * stars[j].vel * dt;
    }

    CRGB c = stars[j].color;

    // If the star is brand new, it flashes white briefly.
    // Otherwise it just fades over time.
    float fade = 0.0f;
    float age = it - stars[j].birth;

    if (age < particleIgnition) {
      c = CRGB(color_blend(WHITE, RGBW32(c.r, c.g, c.b, 0), uint8_t(254.5f * ((age / particleIgnition)))));
    } else {
      // Figure out how much to fade and shrink the star based on
      // its age relative to its lifetime
      if (age > particleIgnition + particleFadeTime) {
        fade = 1.0f;  // Black hole, all faded out
        stars[j].birth = 0;
        c = CRGB(seg.color(1));
      } else {
        age -= particleIgnition;
        fade = (age / particleFadeTime);  // Fading star
        c = CRGB(color_blend(RGBW32(c.r, c.g, c.b, 0), seg.color(1), uint8_t(254.5f * fade)));
      }
    }

    float particleSize = (1.0f - fade) * 2.0f;

    for (size_t index = 0; index < STARBURST_MAX_FRAG * 2; index++) {
      bool mirrored = index & 0x1;
      unsigned i = index >> 1;
      if (stars[j].fragment[i] > 0) {
        float loc = stars[j].fragment[i];
        if (mirrored)
          loc -= (loc - stars[j].pos) * 2;
        // Upstream declares these unsigned, which makes its own `if (start < 0)`
        // clamp dead and turns a negative start into an out of range value.
        // Signed here so the clamp upstream clearly intended actually happens.
        int start = loc - particleSize;
        int end = loc + particleSize;
        if (start < 0)
          start = 0;
        if (start == end)
          end++;
        if (end > (int) seg_len)
          end = seg_len;
        for (int p = start; p < end; p++) {
          seg.set_pixel_color(p, c);
        }
      }
    }
  }
}
#undef STARBURST_MAX_FRAG
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PERCENT
/*
 * Percentage display
 * Intensity values from 0-100 turn on the leds.
 */
void mode_percent(Segment &seg) {
  const unsigned seg_len = seg.length();

  unsigned percent = seg.intensity;
  percent = constrain(percent, 0, 200);
  unsigned active_leds =
      (percent < 100) ? roundf(seg_len * percent / 100.0f) : roundf(seg_len * (200 - percent) / 100.0f);

  unsigned size = (1 + ((seg.speed * seg_len) >> 11));
  if (seg.speed == 255)
    size = 255;

  if (percent <= 100) {
    for (unsigned i = 0; i < seg_len; i++) {
      if (i < seg.aux1) {
        if (seg.check1)
          seg.set_pixel_color(i, seg.color_from_palette(wf_map(percent, 0, 100, 0, 255), false, false, 0));
        else
          seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
      } else {
        seg.set_pixel_color(i, seg.color(1));
      }
    }
  } else {
    for (unsigned i = 0; i < seg_len; i++) {
      if (i < (seg_len - seg.aux1)) {
        seg.set_pixel_color(i, seg.color(1));
      } else {
        if (seg.check1)
          seg.set_pixel_color(i, seg.color_from_palette(wf_map(percent, 100, 200, 255, 0), false, false, 0));
        else
          seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
      }
    }
  }

  if (active_leds > seg.aux1) {  // smooth transition to the target value
    seg.aux1 += size;
    if (seg.aux1 > active_leds)
      seg.aux1 = active_leds;
  } else if (active_leds < seg.aux1) {
    if (seg.aux1 > size)
      seg.aux1 -= size;
    else
      seg.aux1 = 0;
    if (seg.aux1 < active_leds)
      seg.aux1 = active_leds;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HEARTBEAT
/*
 * Modulates the brightness similar to a heartbeat
 * (unimplemented?) tries to draw an ECG approximation on a 2D matrix
 */
void mode_heartbeat(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned bpm = 40 + (seg.speed >> 3);
  uint32_t msPerBeat = (60000L / bpm);
  uint32_t secondBeat = (msPerBeat / 3);
  uint32_t bri_lower = seg.aux1;
  unsigned long beatTimer = seg.now - seg.step;

  bri_lower = bri_lower * 2042 / (2048 + seg.intensity);
  seg.aux1 = bri_lower;

  if ((beatTimer > secondBeat) && !seg.aux0) {  // time for the second beat?
    seg.aux1 = UINT16_MAX;                      // 3/4 bri
    seg.aux0 = 1;
  }
  if (beatTimer > msPerBeat) {  // time to reset the beat timer?
    seg.aux1 = UINT16_MAX;      // full bri
    seg.aux0 = 0;
    seg.step = seg.now;
  }

  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, color_blend(seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0), seg.color(1),
                                       uint8_t(255 - (seg.aux1 >> 8))));
  }
}
#endif

#if WLED_FX_1D_E_PHASED_BASE
/*
 * Effects by Andrew Tuline
 */
void phased_base(Segment &seg, uint8_t moder) {  // We're making sine waves here. By Andrew Tuline.
  const unsigned seg_len = seg.length();

  unsigned allfreq = 16;                                  // Base frequency.
  float *phase = reinterpret_cast<float *>(&seg.step);    // Phase change value gets calculated (float fits into unsigned long).
  unsigned cutOff = (255 - seg.intensity);                // You can change the number of pixels.  AKA INTENSITY (was 192).
  unsigned modVal = 5;  // SEGMENT.fft1/8+1;              // You can change the modulus. AKA FFT1 (was 5).

  unsigned index = seg.now / 64;  // Set color rotation speed
  *phase += seg.speed / 32.0;     // You can change the speed of the wave. AKA SPEED (was .4)

  for (unsigned i = 0; i < seg_len; i++) {
    if (moder == 1)
      modVal = (perlin8(i * 10 + i * 10) / 16);  // Let's randomize our mod length with some Perlin noise.
    unsigned val = (i + 1) * allfreq;            // This sets the frequency of the waves. The +1 makes sure that led 0 is used.
    if (modVal == 0)
      modVal = 1;
    val += *phase * (i % modVal + 1) / 2;  // This sets the varying phase change of the waves. By Andrew Tuline.
    unsigned b = cubicwave8(val);          // Now we make an 8 bit sinewave.
    b = (b > cutOff) ? (b - cutOff) : 0;   // A ternary operator to cutoff the light.
    seg.set_pixel_color(i, color_blend(seg.color(1), seg.color_from_palette(index, false, false, 0), uint8_t(b)));
    index += 256 / seg_len;
    if (seg_len > 256)
      index++;  // Correction for segments longer than 256 LEDs
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PHASED
void mode_phased(Segment &seg) { phased_base(seg, 0); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PHASED_NOISE
void mode_phased_noise(Segment &seg) { phased_base(seg, 1); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLOR_CLOUDS
/** Softly floating colorful clouds.
 * This is a very smooth effect that moves colorful clouds randomly around the LED strip.
 * It was initially intended for rather unobtrusive ambient lights (with very slow speed settings).
 * Nevertheless, it appears completely different and quite vibrant when the sliders are moved near
 * to their limits. No matter in which direction or in which combination...
 * Ported to WLED from https://github.com/JoaDick/EyeCandy/blob/master/ColorClouds.h
 */
void mode_ColorClouds(Segment &seg) {
  const unsigned seg_len = seg.length();
  // Set random start points for clouds and color.
  if (seg.call == 0) {
    seg.aux0 = hw_random16();
    seg.aux1 = hw_random16();
  }
  const uint32_t volX0 = seg.aux0;
  const uint32_t hueX0 = seg.aux1;
  const uint8_t hueOffset0 = volX0 + hueX0;  // derive a 3rd random number

  // Makes a very soft wraparound of the color palette by putting more emphasis on the begin & end
  // of the palette (or on the red'ish colors in case of a rainbow spectrum).
  // This gives the effect oftentimes an even more calm perception.
  const bool cozy = seg.check3;

  // Higher values make the clouds move faster.
  const uint32_t volSpeed = 1 + seg.speed;

  // Higher values make the color change faster.
  const uint32_t hueSpeed = 1 + seg.intensity;

  // Higher values make more clouds (but smaller ones).
  const uint32_t volSqueeze = 8 + seg.custom1;

  // Higher values make the clouds more colorful.
  const uint32_t hueSqueeze = seg.custom2;

  // Higher values make larger gaps between the clouds.
  const int32_t volCutoff = 12500 + seg.custom3 * 900;
  const int32_t volSaturate = 52000;
  // Note: When adjusting these calculations, ensure that volCutoff is always smaller than volSaturate.

  const uint32_t now = seg.now;
  const uint32_t volT = now * volSpeed / 8;
  const uint32_t hueT = now * hueSpeed / 8;
  const uint8_t hueOffset = beat88(64, seg.now) >> 8;

  for (int i = 0; i < (int) seg_len; i++) {
    const uint32_t volX = i * volSqueeze * 64;
    int32_t vol = perlin16(volX0 + volX, volT);
    vol = wf_map(vol, volCutoff, volSaturate, 0, 255);
    vol = constrain(vol, 0, 255);

    const uint32_t hueX = i * hueSqueeze * 8;
    uint8_t hue = perlin16(hueX0 + hueX, hueT) >> 7;
    hue += hueOffset0;
    hue += hueOffset;
    if (cozy) {
      hue = cos8_t(128 + hue / 2);
    }

    uint32_t pixel;
    if (seg.palette) {
      pixel = seg.color_from_palette(hue, false, true, 0, vol);
    } else {
      pixel = CRGBW(CHSV32(hue, uint8_t(255), uint8_t(vol)));
    }

    // Suppress extremely dark pixels to avoid flickering of plain r/g/b.
    if (int(R(pixel)) + G(pixel) + B(pixel) <= 2) {
      pixel = 0;
    }

    seg.set_pixel_color(i, pixel);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SLOW_TRANSITION
/*
 * Slow Transition effect
 * Displays the currently selected palette/color with a very slow transition
 * speed slider controls the number of minutes for the transition (0 = 10s)
 * by DedeHai
 *
 * Upstream also cross-fades the segment CCT. This port has no per-segment CCT
 * (see PORTING.md, deviation 2), so the three CCT fields and the SEGMENT.cct
 * read-back are left out; everything else is unchanged.
 */
typedef struct SlowTransitionData {
  CRGBPalette16 startPalette;    // initial palette
  CRGBPalette16 currentPalette;  // blended palette for current frame, need permanent storage so we can start from this
                                 // if target changes mid transition
  CRGBPalette16 endPalette;      // target palette
  uint8_t startWhite;
  uint8_t currentWhite;
  uint8_t endWhite;
} slow_transition_data;

void mode_slow_transition(Segment &seg) {
  const unsigned seg_len = seg.length();
  // aliases
  uint32_t *startTime = &seg.step;  // use step to store start time of transition
  uint16_t *stepsDone = &seg.aux0;
  uint16_t *startSpeed = &seg.aux1;  // speed setting at the start of the transition, used to detect changes

  size_t dataSize = sizeof(slow_transition_data);
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;
  slow_transition_data *data = reinterpret_cast<slow_transition_data *>(seg.data);
  bool changed = (data->endPalette != seg.palette_ref() || *startSpeed != seg.speed ||
                  data->endWhite != W(seg.color(0)));  // detect changes in target color or speed setting

  // (re) init
  if (changed || seg.call == 0) {
    if (seg.call == 0) {
      data->startPalette = seg.palette_ref();
      data->currentPalette = seg.palette_ref();
      data->endPalette = seg.palette_ref();
      data->startWhite = data->currentWhite = data->endWhite = W(seg.color(0));
      *stepsDone = 0xFFFF;  // set to max, fading will start once a change is detected
    } else {
      data->startPalette = data->currentPalette;
      data->endPalette = seg.palette_ref();
      data->startWhite = data->currentWhite;
      data->endWhite = W(seg.color(0));
      *stepsDone = 0;  // reset counter
    }
    *startSpeed = seg.speed;
    *startTime = seg.now;  // set start time, seg.now is the real time clock in milliseconds
  }

  uint32_t totalSteps = seg.check2 ? 16 * 255 : 255;
  uint32_t duration =
      (seg.speed == 0) ? 10000
                       : (uint32_t) seg.speed * 60000;  // 10s if zero (good for testing), otherwise map 1-255 to 1-255 minutes
  uint32_t elapsed = seg.now - *startTime;  // note: will overflow after ~50 days if just left alone (edge case unhandled)
  uint32_t expectedSteps = (uint64_t) elapsed * totalSteps / duration;
  expectedSteps = expectedSteps < totalSteps ? expectedSteps : totalSteps;  // limit to total steps

  if (*stepsDone > expectedSteps)
    *stepsDone = expectedSteps;  // in case sweep was disabled mid transition

  if (*stepsDone < expectedSteps) {
    *stepsDone = expectedSteps;  // jump to expected steps to make sure timing is correct (need up to 4080 frames, at
                                 // 20fps that is ~200 seconds)
    uint8_t blendAmount;
    if (seg.check2) {
      // sweep: one palette entry at a time
      uint8_t i = *stepsDone % 16;
      blendAmount = *stepsDone / 16;
      data->currentPalette[i] =
          CRGB(color_blend(CRGBW(data->startPalette[i]), CRGBW(data->endPalette[i]), blendAmount));
    } else {
      // full palette at once
      blendAmount = (uint8_t) *stepsDone;
      for (uint8_t i = 0; i < 16; i++) {
        data->currentPalette[i] =
            CRGB(color_blend(CRGBW(data->startPalette[i]), CRGBW(data->endPalette[i]), blendAmount));
      }
    }
    data->currentWhite = (data->startWhite * (255 - blendAmount) + data->endWhite * blendAmount) / 255;
    if (*stepsDone >= totalSteps) {
      // transition complete, apply end palette
      data->currentPalette = data->endPalette;  // set to end palette (sweep may not have set all entries)
      data->currentWhite = data->endWhite;
    }
  }
  // display current palette (plus white) over segment
  for (unsigned i = 0; i < seg_len; i++) {
    uint8_t paletteIndex = (i * 255) / seg_len;
    CRGBW palcol = ColorFromPalette(data->currentPalette, paletteIndex, 255, LINEARBLEND_NOWRAP);
    palcol.w = data->currentWhite;  // TODO: currently "sweep mode" does not support white sweep
    seg.set_pixel_color(i, palcol.color32);
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ANDROID
    {"Android@!,Width;!,!;!;;m12=1", mode_android},  // vertical
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE
    {"Chase@!,Width;!,!,!;!", mode_chase_color},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_RANDOM
    {"Chase Random@!,Width;!,,!;!", mode_chase_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_RAINBOW
    {"Chase Rainbow@!,Width;!,!;!", mode_chase_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAINBOW_RUNNER
    {"Rainbow Runner@!,Size;Bg;!", mode_chase_rainbow_white},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LIGHTHOUSE
    {"Lighthouse@!,Fade rate;!,!;!", mode_comet},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_3
    {"Chase 3@!,Size;1,2,3;!", mode_tricolor_chase},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MULTI_COMET
    {"Multi Comet@!,Fade;!,!;!;1", mode_multi_comet},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STREAM_2
    {"Stream 2@!;;", mode_random_chase},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LIGHTNING
    {"Lightning@!,!,,,,,Overlay;!,!;!", mode_lightning},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORWAVES
    {"Colorwaves@!,Hue;!;!;;pal=26", mode_colorwaves},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BPM
    {"Bpm@!;!;!;;sx=64", mode_bpm},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LAKE
    {"Lake@!;Fx;!", mode_lake},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_PATTERN
    {"Solid Pattern@Fg size,Bg size;Fg,!;!;;pal=0", mode_static_pattern},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BOUNCING_BALLS
    {"Bouncing Balls@Gravity,# of balls,,,,,Overlay;!,!,!;!;1;m12=1", mode_bouncing_balls},  // bar
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CANDLE
    {"Candle@!,!;!,!;!;01;sx=96,ix=224,pal=0", mode_candle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CANDLE_MULTI
    {"Candle Multi@!,!;!,!;!;;sx=96,ix=224,pal=0", mode_candle_multi},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_STARBURST
    {"Fireworks Starburst@Chance,Fragments,,,,,Overlay;,!;!;;pal=11,m12=0", mode_starburst},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PERCENT
    {"Percent@!,% of fill,,,,One color;!,!;!", mode_percent},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HEARTBEAT
    {"Heartbeat@!,!;!,!;!;01;m12=1", mode_heartbeat},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PHASED
    {"Phased@!,!;!,!;!", mode_phased},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PHASED_NOISE
    {"Phased Noise@!,!;!,!;!", mode_phased_noise},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLOR_CLOUDS
    {"Color Clouds@!,!,Clouds,Colors,Distance,,,Cozy;;!;;sx=24,ix=32,c1=48,c2=64,c3=12,pal=0", mode_ColorClouds},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SLOW_TRANSITION
    {"Slow Transition@Time (min),,,,,,Sweep;!;!;1;pal=2,sx=0,ix=0", mode_slow_transition},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_E;
const EffectGroup EFFECT_GROUP_1D_E{"1d_e", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_E
