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

/* 1D batch D. See BATCHES.md for the effect list this file owns. */

#include <algorithm>

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_1D_D                                                                            \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER || WLED_FX_FX_THEATER_RAINBOW || WLED_FX_FX_CHASE_2 || \
   WLED_FX_FX_RUNNING || WLED_FX_FX_SAW || WLED_FX_FX_RUNNING_DUAL || WLED_FX_FX_CHASE_FLASH ||        \
   WLED_FX_FX_CHASE_FLASH_RND || WLED_FX_FX_RANDOM_COLORS || WLED_FX_FX_PACIFICA || WLED_FX_FX_NOISE_2 || \
   WLED_FX_FX_NOISE_3 || WLED_FX_FX_NOISE_4 || WLED_FX_FX_FILL_NOISE || WLED_FX_FX_NOISE_PAL ||        \
   WLED_FX_FX_SPARKLE || WLED_FX_FX_SPARKLE_PLUS || WLED_FX_FX_SPARKLE_DARK ||                        \
   WLED_FX_FX_DANCING_SHADOWS || WLED_FX_FX_SHIMMER ||                                                \
   WLED_FX_FX_POPCORN || WLED_FX_FX_COLORFUL || WLED_FX_FX_AURORA || WLED_FX_FX_TRI_WIPE ||            \
   WLED_FX_FX_FLOW || WLED_FX_FX_RAILWAY || WLED_FX_FX_STROBE_MEGA || WLED_FX_FX_BLENDS ||             \
   WLED_FX_FX_BREATHE || WLED_FX_FX_FLOW_STRIPE)

#if WLED_FX_GROUP_1D_D

namespace esphome {
namespace wled_fx {
namespace {

// ---------------------------------------------------------------------------
// Shared upstream helpers this batch owns. Translation units never share these,
// so another batch carries its own copy of the same code (PORTING.md section 8).
// ---------------------------------------------------------------------------

// sin_gap(), get_random_wheel_index(), the Spark struct, NUM_COLORS and
// FAIR_DATA_PER_SEG are engine code now, in wf_fx_shared.h and wf_segment.h.

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RANDOM_COLORS
/*
 * Lights all LEDs up in one random color. Then switches them
 * to the next random color.
 */
void mode_random_color(Segment &seg) {
  uint32_t cycleTime = 200 + (255 - seg.speed) * 50;
  uint32_t it = seg.now / cycleTime;
  uint32_t rem = seg.now % cycleTime;
  unsigned fadedur = (cycleTime * seg.intensity) >> 8;

  uint32_t fade = 255;
  if (fadedur) {
    fade = (rem * 255) / fadedur;
    if (fade > 255)
      fade = 255;
  }

  if (seg.call == 0) {
    seg.aux0 = hw_random8();
    seg.step = 2;
  }
  if (it != seg.step)  // new color
  {
    seg.aux1 = seg.aux0;
    seg.aux0 = get_random_wheel_index(seg.aux0);  // aux0 will store our random color wheel index
    seg.step = it;
  }

  seg.fill(color_blend(seg.color_wheel(seg.aux1), seg.color_wheel(seg.aux0), uint8_t(fade)));
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BREATHE
/*
 * Does the "standby-breathing" of well known i-Devices.
 */
void mode_breath(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned var = 0;
  unsigned counter = (seg.now * ((seg.speed >> 3) + 10)) & 0xFFFFU;
  counter = (counter >> 2) + (counter >> 4);  // 0-16384 + 0-2048
  if (counter < 16384) {
    if (counter > 8192)
      counter = 8192 - (counter - 8192);
    var = sin16_t(counter) / 103;  // close to parabolic in range 0-8192, max val. 23170
  }

  uint8_t lum = 30 + var;
  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, color_blend(seg.color(1), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0), lum));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER || WLED_FX_FX_THEATER_RAINBOW || WLED_FX_FX_CHASE_2
/*
 * Alternating pixels running function.
 */
void running(Segment &seg, uint32_t color1, uint32_t color2, bool theatre = false) {
  const unsigned seg_len = seg.length();
  int width = (theatre ? 3 : 1) + (seg.intensity >> 4);  // window
  uint32_t cycleTime = 50 + (255 - seg.speed);
  uint32_t it = seg.now / cycleTime;
  bool usePalette = color1 == seg.color(0);

  for (unsigned i = 0; i < seg_len; i++) {
    uint32_t col = color2;
    if (usePalette)
      color1 = seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0);
    if (theatre) {
      if ((i % width) == seg.aux0)
        col = color1;
    } else {
      int pos = (i % (width << 1));
      if ((pos < seg.aux0 - width) || ((pos >= seg.aux0) && (pos < seg.aux0 + width)))
        col = color1;
    }
    seg.set_pixel_color(i, col);
  }

  if (it != seg.step) {
    seg.aux0 = (seg.aux0 + 1) % (theatre ? width : (width << 1));
    seg.step = it;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER
/*
 * Theatre-style crawling lights.
 * Inspired by the Adafruit examples.
 */
void mode_theater_chase(Segment &seg) { running(seg, seg.color(0), seg.color(1), true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER_RAINBOW
/*
 * Theatre-style crawling lights with rainbow effect.
 * Inspired by the Adafruit examples.
 */
void mode_theater_chase_rainbow(Segment &seg) { running(seg, seg.color_wheel(seg.step), seg.color(1), true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_2
/*
 * Alternating color/sec pixels running.
 */
void mode_running_color(Segment &seg) { running(seg, seg.color(0), seg.color(1)); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RUNNING || WLED_FX_FX_SAW || WLED_FX_FX_RUNNING_DUAL
/*
 * Running lights effect with smooth sine transition base.
 */
void running_base(Segment &seg, bool saw, bool dual = false) {
  const unsigned seg_len = seg.length();
  unsigned x_scale = seg.intensity >> 2;
  uint32_t counter = (seg.now * seg.speed) >> 9;

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned a = i * x_scale - counter;
    if (saw) {
      a &= 0xFF;
      if (a < 16) {
        a = 192 + a * 8;
      } else {
        a = wf_map(a, 16, 255, 64, 192);
      }
      a = 255 - a;
    }
    uint8_t s = dual ? sin_gap(a) : sin8_t(a);
    uint32_t ca = color_blend(seg.color(1), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0), s);
    if (dual) {
      unsigned b = (seg_len - 1 - i) * x_scale - counter;
      uint8_t t = sin_gap(b);
      uint32_t cb = color_blend(seg.color(1), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 2), t);
      ca = color_blend(ca, cb, uint8_t(127));
    }
    seg.set_pixel_color(i, ca);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RUNNING_DUAL
/*
 * Running lights in opposite directions.
 * Idea: Make the gap width controllable with a third slider in the future
 */
void mode_running_dual(Segment &seg) { running_base(seg, false, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RUNNING
/*
 * Running lights effect with smooth sine transition.
 */
void mode_running_lights(Segment &seg) { running_base(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SAW
/*
 * Running lights effect with sawtooth transition.
 */
void mode_saw(Segment &seg) { running_base(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE
/*
 * Blinks one LED at a time.
 * Inspired by www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/
 */
void mode_sparkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.check2)
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1));
    }
  uint32_t cycleTime = 10 + (255 - seg.speed) * 2;
  uint32_t it = seg.now / cycleTime;
  if (it != seg.step) {
    seg.aux0 = hw_random16(seg_len);  // aux0 stores the random led index
    seg.step = it;
  }

  seg.set_pixel_color(seg.aux0, seg.color(0));
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE_DARK
/*
 * Lights all LEDs in the color. Flashes single col 1 pixels randomly. (List name: Sparkle Dark)
 * Inspired by www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/
 */
void mode_flash_sparkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.check2)
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
    }

  if (seg.now - seg.aux0 > seg.step) {
    if (hw_random8((255 - seg.intensity) >> 4) == 0) {
      seg.set_pixel_color(hw_random16(seg_len), seg.color(1));  // flash
    }
    seg.step = seg.now;
    seg.aux0 = 255 - seg.speed;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE_PLUS
/*
 * Like flash sparkle. With more flash.
 * Inspired by www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/
 */
void mode_hyper_sparkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.check2)
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
    }

  if (seg.now - seg.aux0 > seg.step) {
    if (hw_random8((255 - seg.intensity) >> 4) == 0) {
      int len = std::max(1, (int) seg_len / 3);
      for (int i = 0; i < len; i++) {
        seg.set_pixel_color(hw_random16(seg_len), seg.color(1));
      }
    }
    seg.step = seg.now;
    seg.aux0 = 255 - seg.speed;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE_MEGA
/*
 * Strobe effect with different strobe count and pause, controlled by speed.
 */
void mode_multi_strobe(Segment &seg) {
  const unsigned seg_len = seg.length();
  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1));
  }

  seg.aux0 = 50 + 20 * (uint16_t) (255 - seg.speed);
  unsigned count = 2 * ((seg.intensity / 10) + 1);
  if (seg.aux1 < count) {
    if ((seg.aux1 & 1) == 0) {
      seg.fill(seg.color(0));
      seg.aux0 = 15;
    } else {
      seg.aux0 = 50;
    }
  }

  if (seg.now - seg.aux0 > seg.step) {
    seg.aux1++;
    if (seg.aux1 > count)
      seg.aux1 = 0;
    seg.step = seg.now;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORFUL
/*
 * Red - Amber - Green - Blue lights running
 */
void mode_colorful(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned numColors = 4;  // 3, 4, or 5
  uint32_t cols[9]{0x00FF0000, 0x00EEBB00, 0x0000EE00, 0x000077CC};
  if (seg.intensity > 160 || seg.palette) {  // palette or color
    if (!seg.palette) {
      numColors = 3;
      for (size_t i = 0; i < 3; i++)
        cols[i] = seg.color(i);
    } else {
      unsigned fac = 80;
      if (seg.palette == 52) {
        numColors = 5;
        fac = 61;
      }  // C9 2 has 5 colors
      for (size_t i = 0; i < numColors; i++) {
        cols[i] = seg.color_from_palette(i * fac, false, true, 255);
      }
    }
  } else if (seg.intensity < 80)  // pastel (easter) colors
  {
    cols[0] = 0x00FF8040;
    cols[1] = 0x00E5D241;
    cols[2] = 0x0077FF77;
    cols[3] = 0x0077F0F0;
  }
  for (size_t i = numColors; i < numColors * 2 - 1U; i++)
    cols[i] = cols[i - numColors];

  uint32_t cycleTime = 50 + (8 * (uint32_t) (255 - seg.speed));
  uint32_t it = seg.now / cycleTime;
  if (it != seg.step) {
    if (seg.speed > 0)
      seg.aux0++;
    if (seg.aux0 >= numColors)
      seg.aux0 = 0;
    seg.step = it;
  }

  for (unsigned i = 0; i < seg_len; i += numColors) {
    for (unsigned j = 0; j < numColors; j++)
      seg.set_pixel_color(i + j, cols[seg.aux0 + j]);
  }
}
#endif

/*
 * Sec flashes running on prim.
 */
#define FLASH_COUNT 4

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_FLASH
void mode_chase_flash(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned now = seg.now;  // save time for delay calculation
  bool advance = true;
  unsigned flash_step = seg.aux1 % ((FLASH_COUNT * 2) + 1);
  if (now < seg.step)
    advance = false;  // limit update rate but render every frame for smooth transitions
  else
    seg.aux1++;

  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0));
  }
  unsigned index = seg.aux0;
  unsigned n = index;
  unsigned m = (index + 1) % seg_len;

  unsigned delay = 10 + ((30 * (uint16_t) (255 - seg.speed)) / seg_len);
  if (flash_step < (FLASH_COUNT * 2)) {
    if (flash_step % 2 == 0) {
      seg.set_pixel_color(n, seg.color(1));
      seg.set_pixel_color(m, seg.color(1));
      delay = 20;
    } else {
      delay = 30;
    }
  } else if (advance) {
    seg.aux0 = m;  // advance to next position
  }
  if (advance)
    seg.step = now + delay;  // set next update time
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_FLASH_RND
/*
 * Prim flashes running, followed by random color.
 */
void mode_chase_flash_random(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned now = seg.now;  // save time for delay calculation
  bool advance = true;
  if (now < seg.step) {
    seg.call--;  // revert increment to skip moving the animation forward and just render the same frame again
    advance = false;
  }
  unsigned flash_step = seg.call % ((FLASH_COUNT * 2) + 1);

  for (int i = 0; i < seg.aux1; i++) {
    seg.set_pixel_color(i, seg.color_wheel(seg.aux0));
  }

  unsigned delay = 1 + ((10 * (uint16_t) (255 - seg.speed)) / seg_len);
  if (flash_step < (FLASH_COUNT * 2)) {
    unsigned n = seg.aux1;
    unsigned m = (seg.aux1 + 1) % seg_len;
    if (flash_step % 2 == 0) {
      seg.set_pixel_color(n, seg.color(0));
      seg.set_pixel_color(m, seg.color(0));
      delay = 20;
    } else {
      seg.set_pixel_color(n, seg.color_wheel(seg.aux0));
      seg.set_pixel_color(m, seg.color(1));
      delay = 30;
    }
  } else if (advance) {
    seg.aux1 = (seg.aux1 + 1) % seg_len;

    if (seg.aux1 == 0) {
      seg.aux0 = get_random_wheel_index(seg.aux0);
    }
  }
  if (advance)
    seg.step = now + delay;  // set next update time
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRI_WIPE
/*
 * Custom mode by Aircoookie. Color Wipe, but with 3 colors
 */
void mode_tricolor_wipe(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = 1000 + (255 - seg.speed) * 200;
  uint32_t perc = seg.now % cycleTime;
  unsigned prog = (perc * 65535) / cycleTime;
  unsigned ledIndex = (prog * seg_len * 3) >> 16;
  unsigned ledOffset = ledIndex;

  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 2));
  }

  if (ledIndex < seg_len) {  // wipe from 0 to 1
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, (i > ledOffset) ? seg.color(0) : seg.color(1));
    }
  } else if (ledIndex < seg_len * 2) {  // wipe from 1 to 2
    ledOffset = ledIndex - seg_len;
    for (unsigned i = ledOffset + 1; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color(1));
    }
  } else  // wipe from 2 to 0
  {
    ledOffset = ledIndex - seg_len * 2;
    for (unsigned i = 0; i <= ledOffset; i++) {
      seg.set_pixel_color(i, seg.color(0));
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FILL_NOISE
void mode_fillnoise8(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg.call == 0)
    seg.step = hw_random();
  for (unsigned i = 0; i < seg_len; i++) {
    unsigned index = perlin8(i * seg_len, seg.step + i * seg_len);
    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
  }
  seg.step += beatsin8_t(seg.speed, 1, 6, seg.now);  // 10,1,4
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_2
void mode_noise16_2(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned scale = 1000;  // the "zoom factor" for the noise
  seg.step += (1 + (seg.speed >> 1));

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned shift_x = seg.step >> 6;                 // x as a function of time
    uint32_t real_x = (i + shift_x) * scale;          // calculate the coordinates within the noise field
    unsigned noise = perlin16(real_x, 0, 4223) >> 8;  // get the noise data and scale it down
    unsigned index = sin8_t(noise * 3);               // map led color based on noise data

    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0, noise));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_3
void mode_noise16_3(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned scale = 800;  // the "zoom factor" for the noise
  seg.step += (1 + seg.speed);

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned shift_x = 4223;  // no movement along x and y
    unsigned shift_y = 1234;
    uint32_t real_x = (i + shift_x) * scale;                // calculate the coordinates within the noise field
    uint32_t real_y = (i + shift_y) * scale;                // based on the precalculated positions
    uint32_t real_z = seg.step * 8;
    unsigned noise = perlin16(real_x, real_y, real_z) >> 8;  // get the noise data and scale it down
    unsigned index = sin8_t(noise * 3);                      // map led color based on noise data

    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0, noise));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_4
// https://github.com/aykevl/ledstrip-spark/blob/master/ledstrip.ino
void mode_noise16_4(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t stp = (seg.now * seg.speed) >> 7;
  for (unsigned i = 0; i < seg_len; i++) {
    int index = perlin16(uint32_t(i) << 12, stp);
    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAILWAY
// Railway Crossing / Christmas Fairy lights
void mode_railway(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned dur = (256 - seg.speed) * 40;
  uint16_t rampdur = (dur * seg.intensity) >> 8;
  if (seg.step > dur) {
    // reverse direction
    seg.step = 0;
    seg.aux0 = !seg.aux0;
  }
  unsigned pos = 255;
  if (rampdur != 0) {
    unsigned p0 = (seg.step * 255) / rampdur;
    if (p0 < 255)
      pos = p0;
  }
  if (seg.aux0)
    pos = 255 - pos;
  for (unsigned i = 0; i < seg_len; i += 2) {
    seg.set_pixel_color(i, seg.color_from_palette(255 - pos, false, false, 255));  // do not use color 1 or 2, always
                                                                                   // use palette
    if (i < seg_len - 1) {
      seg.set_pixel_color(i + 1, seg.color_from_palette(pos, false, false, 255));  // do not use color 1 or 2, always
                                                                                   // use palette
    }
  }
  seg.step += FRAMETIME;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POPCORN
#define maxNumPopcorn 21  // max 21 on 16 segment ESP8266
/*
 *  POPCORN
 *  modified from https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/Popcorn.h
 */
void mode_popcorn(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // allocate segment data
  unsigned strips = seg.nr_of_v_strips();
  unsigned usablePopcorns = maxNumPopcorn;
  if (usablePopcorns * strips * sizeof(spark) > FAIR_DATA_PER_SEG)
    usablePopcorns = FAIR_DATA_PER_SEG / (strips * sizeof(spark)) + 1;  // at least 1 popcorn per vstrip
  unsigned dataSize =
      sizeof(spark) * usablePopcorns;  // on a matrix 64x64 this could consume a little less than 27kB when Bar
                                       // expansion is used
  if (!seg.allocate_data(dataSize * strips))
    FX_FALLBACK_STATIC;  // allocation failed

  Spark *popcorn = reinterpret_cast<Spark *>(seg.data);

  bool hasCol2 = seg.color(2);
  if (!seg.check2)
    seg.fill(hasCol2 ? BLACK : seg.color(1));

  const auto run_strip = [&](uint16_t stripNr, Spark *popcorn, unsigned usablePopcorns) {
    float gravity = -0.0001f - (seg.speed / 200000.0f);  // m/s/s
    gravity *= seg_len;

    unsigned numPopcorn = seg.intensity * usablePopcorns / 255;
    if (numPopcorn == 0)
      numPopcorn = 1;

    for (unsigned i = 0; i < numPopcorn; i++) {
      if (popcorn[i].pos >= 0.0f) {  // if kernel is active, update its position
        popcorn[i].pos += popcorn[i].vel;
        popcorn[i].vel += gravity;
      } else {  // if kernel is inactive, randomly pop it
        if (hw_random8() < 2) {  // POP!!!
          popcorn[i].pos = 0.01f;

          unsigned peakHeight = 128 + hw_random8(128);  // 0-255
          peakHeight = (peakHeight * (seg_len - 1)) >> 8;
          popcorn[i].vel = sqrtf(-2.0f * gravity * peakHeight);

          if (seg.palette) {
            popcorn[i].colIndex = hw_random8();
          } else {
            uint8_t col = hw_random8(0, NUM_COLORS);
            if (!seg.color(2) || !seg.color(col))
              col = 0;
            popcorn[i].colIndex = col;
          }
        }
      }
      if (popcorn[i].pos >= 0.0f) {  // draw now active popcorn (either active before or just popped)
        uint32_t col = seg.color_wheel(popcorn[i].colIndex);
        if (!seg.palette && popcorn[i].colIndex < NUM_COLORS)
          col = seg.color(popcorn[i].colIndex);
        unsigned ledIndex = popcorn[i].pos;
        if (ledIndex < seg_len)
          seg.set_pixel_color(Segment::index_to_v_strip(ledIndex, stripNr), col);
      }
    }
  };

  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &popcorn[stripNr * usablePopcorns], usablePopcorns);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PACIFICA
//////////////////////////////////////////////////////////////////////////////////////////
//
//  "Pacifica"
//  Gentle, blue-green ocean waves.
//  December 2019, Mark Kriegsman and Mary Corey March.
//  For Dan.
//
// Add one layer of waves into the led array
CRGB pacifica_one_layer(Segment &seg, uint16_t i, const CRGBPalette16 &p, uint16_t cistart, uint16_t wavescale,
                        uint8_t bri, uint16_t ioff) {
  unsigned ci = cistart;
  unsigned waveangle = ioff;
  unsigned wavescale_half = (wavescale >> 1) + 20;

  waveangle += ((120 + seg.intensity) * i);  // original 250 * i
  unsigned s16 = sin16_t(waveangle) + 32768;
  unsigned cs = scale16(s16, wavescale_half) + wavescale_half;
  ci += (cs * i);
  unsigned sindex16 = sin16_t(ci) + 32768;
  unsigned sindex8 = scale16(sindex16, 240);
  return CRGB(ColorFromPalette(p, sindex8, bri, LINEARBLEND));
}

void mode_pacifica(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t nowOld = seg.now;

  CRGBPalette16 pacifica_palette_1 = {
      0x002229, 0x001E2F, 0x001934, 0x001938, 0x00143F, 0x001443, 0x00B047,
      0x00B04C,  // note: palettes are gamma inverted using gamma 2.0 to get closer to pre 16.0 looks
      0x00004F, 0x000054, 0x000062, 0x00006F, 0x00007A, 0x000085, 0x47938A, 0x64D08E};
  CRGBPalette16 pacifica_palette_2 = {0x002229, 0x001E2F, 0x001934, 0x001938, 0x00143F, 0x001443, 0x00B047, 0x00B04C,
                                      0x00004F, 0x000054, 0x000062, 0x00006F, 0x00007A, 0x000085, 0x369B90, 0x4FDC9B};
  CRGBPalette16 pacifica_palette_3 = {0x00142C, 0x00193B, 0x002247, 0x002551, 0x002C5A, 0x002F63, 0x00346B, 0x003671,
                                      0x003B78, 0x003F7F, 0x00478E, 0x004D9C, 0x0054A9, 0x005AB4, 0x3F7FDC, 0x5A9CFF};

  if (seg.palette) {
    pacifica_palette_1 = seg.palette_ref();
    pacifica_palette_2 = seg.palette_ref();
    pacifica_palette_3 = seg.palette_ref();
  }

  // Increment the four "color index start" counters, one for each wave layer.
  // Each is incremented at a different speed, and the speeds vary over time.
  unsigned sCIStart1 = seg.aux0, sCIStart2 = seg.aux1, sCIStart3 = seg.step & 0xFFFF, sCIStart4 = (seg.step >> 16);
  uint32_t deltams = (FRAMETIME >> 2) + ((FRAMETIME * seg.speed) >> 7);
  uint64_t deltat = (seg.now >> 2) + ((seg.now * seg.speed) >> 7);
  seg.now = deltat;

  unsigned speedfactor1 = beatsin16_t(3, 179, 269, seg.now);
  unsigned speedfactor2 = beatsin16_t(4, 179, 269, seg.now);
  uint32_t deltams1 = (deltams * speedfactor1) / 256;
  uint32_t deltams2 = (deltams * speedfactor2) / 256;
  uint32_t deltams21 = (deltams1 + deltams2) / 2;
  sCIStart1 += (deltams1 * beatsin88_t(1011, 10, 13, seg.now));
  sCIStart2 -= (deltams21 * beatsin88_t(777, 8, 11, seg.now));
  sCIStart3 -= (deltams1 * beatsin88_t(501, 5, 7, seg.now));
  sCIStart4 -= (deltams2 * beatsin88_t(257, 4, 6, seg.now));
  seg.aux0 = sCIStart1;
  seg.aux1 = sCIStart2;
  seg.step = (sCIStart4 << 16) | (sCIStart3 & 0xFFFF);

  // Clear out the LED array to a dim background blue-green
  // seg.fill(132618);

  unsigned basethreshold = beatsin8_t(9, 55, 65, seg.now);
  unsigned wave = beat8(7, seg.now);

  for (unsigned i = 0; i < seg_len; i++) {
    CRGB c = CRGB(2, 6, 10);
    // Render each of four layers, with different scales and speeds, that vary over time
    c += pacifica_one_layer(seg, i, pacifica_palette_1, sCIStart1, beatsin16_t(3, 11 * 256, 14 * 256, seg.now),
                            beatsin8_t(10, 70, 130, seg.now), 0 - beat16(301, seg.now));
    c += pacifica_one_layer(seg, i, pacifica_palette_2, sCIStart2, beatsin16_t(4, 6 * 256, 9 * 256, seg.now),
                            beatsin8_t(17, 40, 80, seg.now), beat16(401, seg.now));
    c += pacifica_one_layer(seg, i, pacifica_palette_3, sCIStart3, 6 * 256, beatsin8_t(9, 10, 38, seg.now),
                            0 - beat16(503, seg.now));
    c += pacifica_one_layer(seg, i, pacifica_palette_3, sCIStart4, 5 * 256, beatsin8_t(8, 10, 28, seg.now),
                            beat16(601, seg.now));

    // Add extra 'white' to areas where the four layers of light have lined up brightly
    unsigned threshold = scale8(sin8_t(wave), 20) + basethreshold;
    wave += 7;
    unsigned l = c.getAverageLight();
    if (l > threshold) {
      unsigned overage = l - threshold;
      unsigned overage2 = qadd8(overage, overage);
      c += CRGB(overage, overage2, qadd8(overage2, overage2));
    }

    // deepen the blues and greens  note: no longer needed with proper gamma in 16.0
    // c.blue  = scale8(c.blue,  145);
    // c.green = scale8(c.green, 200);
    // c |= CRGB( 2, 5, 7);

    seg.set_pixel_color(i, c);
  }

  seg.now = nowOld;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_PAL
// Peaceful noise that's slow and with gradually changing palettes. Does not support WLED palettes or default colours
// or controls.
void mode_noisepal(Segment &seg) {  // Slow noise palette by Andrew Tuline.
  const unsigned seg_len = seg.length();
  unsigned scale = 15 + (seg.intensity >> 2);  // default was 30
  // #define scale 30

  unsigned dataSize = sizeof(CRGBPalette16) * 2;  // allocate space for 2 Palettes (2 * 16 * 3 = 96 bytes)
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  CRGBPalette16 *palettes = reinterpret_cast<CRGBPalette16 *>(seg.data);

  unsigned changePaletteMs = 4000 + seg.speed * 10;  // between 4 - 6.5sec
  if (seg.now - seg.step > changePaletteMs) {
    seg.step = seg.now;

    unsigned baseI = hw_random8();
    uint32_t minBri = gamma8inv(128);  // use gamma inversion on min brightness value to restore pre 16.0 looks (more
                                       // brilliant palettes)
    palettes[1] = CRGBPalette16(CHSV(baseI + hw_random8(64), 255, hw_random8(minBri, 255)),
                                CHSV(baseI + 128, 255, hw_random8(minBri, 255)),
                                CHSV(baseI + hw_random8(92), 192, hw_random8(minBri, 255)),
                                CHSV(baseI + hw_random8(92), 255, hw_random8(minBri, 255)));
  }

  // EVERY_N_MILLIS(10) { //(don't have to time this, effect function is only called every 24ms)
  nblendPaletteTowardPalette(palettes[0], palettes[1], 48);  // Blend towards the target palette over 48 iterations.

  if (seg.palette > 0)
    palettes[0] = seg.palette_ref();

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned index = perlin8(i * scale, seg.aux0 + i * scale);  // Get a value from the noise function. I'm using both
                                                                // x and y axis.
    seg.set_pixel_color(i, ColorFromPalette(palettes[0], index, 255, LINEARBLEND));  // Use my own palette.
  }

  seg.aux0 += beatsin8_t(10, 1, 4, seg.now);  // Moving along the distance. Vary it a bit with a sine wave.
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FLOW
/*
 * Best of both worlds from Palette and Spot effects. By Aircoookie
 */
void mode_flow(Segment &seg) {
  const unsigned seg_len = seg.length();
  // Added guard: upstream divides by zoneLen, which is zero on a one pixel
  // segment. See the "Division by seg_len" pitfall in PORTING.md.
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned counter = 0;
  if (seg.speed != 0) {
    counter = seg.now * ((seg.speed >> 2) + 1);
    counter = counter >> 8;
  }

  unsigned maxZones = seg_len / 6;  // only looks good if each zone has at least 6 LEDs
  int zones = (seg.intensity * maxZones) >> 8;
  if (zones & 0x01)
    zones++;  // zones must be even
  if (zones < 2)
    zones = 2;
  int zoneLen = seg_len / zones;
  int requiredZones = (seg_len + zoneLen - 1) / zoneLen;
  zones = requiredZones + 2;  // add extra zones to cover beginning and end of segment (compensate integer truncation)
  int offset = ((int) seg_len - (zones * zoneLen)) / 2;  // center the zones on the segment (can not use bit shift on
                                                         // negative number)

  for (int z = 0; z < zones; z++) {
    int pos = offset + z * zoneLen;
    for (int i = 0; i < zoneLen; i++) {
      unsigned colorIndex = (i * 255 / zoneLen) - counter;
      int led = (z & 0x01) ? i : (zoneLen - 1) - i;
      seg.set_pixel_color(pos + led, seg.color_from_palette(colorIndex, false, true, 255));
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DANCING_SHADOWS
constexpr uint8_t SPOT_TYPE_SOLID = 0;
constexpr uint8_t SPOT_TYPE_GRADIENT = 1;
constexpr uint8_t SPOT_TYPE_2X_GRADIENT = 2;
constexpr uint8_t SPOT_TYPE_2X_DOT = 3;
constexpr uint8_t SPOT_TYPE_3X_DOT = 4;
constexpr uint8_t SPOT_TYPE_4X_DOT = 5;
constexpr uint8_t SPOT_TYPES_COUNT = 6;
constexpr uint8_t SPOT_MAX_COUNT = 49;  // Number of simultaneous waves

// 13 bytes
typedef struct Spotlight {
  float speed;
  uint8_t colorIdx;
  int16_t position;
  unsigned long lastUpdateTime;
  uint8_t width;
  uint8_t type;
} spotlight;

/*
 * Spotlights moving back and forth that cast dancing shadows.
 * Shine this through tree branches/leaves or other close-up objects that cast
 * interesting shadows onto a ceiling or tarp.
 *
 * By Steve Pomeroy @xxv
 */
void mode_dancing_shadows(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned numSpotlights =
      wf_map(seg.intensity, 0, 255, 2, SPOT_MAX_COUNT);  // 49 on 32 segment ESP32, 17 on 16 segment ESP8266
  bool initialize = seg.aux0 != numSpotlights;
  seg.aux0 = numSpotlights;

  unsigned dataSize = sizeof(spotlight) * numSpotlights;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  Spotlight *spotlights = reinterpret_cast<Spotlight *>(seg.data);

  seg.fill(BLACK);

  unsigned long time = seg.now;
  bool respawn = false;

  for (size_t i = 0; i < numSpotlights; i++) {
    if (!initialize) {
      // advance the position of the spotlight
      int delta = (float) (time - spotlights[i].lastUpdateTime) * (spotlights[i].speed * ((1.0 + seg.speed) / 100.0));

      if (abs(delta) >= 1) {
        spotlights[i].position += delta;
        spotlights[i].lastUpdateTime = time;
      }

      respawn = (spotlights[i].speed > 0.0 && spotlights[i].position > (int) (seg_len + 2)) ||
                (spotlights[i].speed < 0.0 && spotlights[i].position < -(spotlights[i].width + 2));
    }

    if (initialize || respawn) {
      spotlights[i].colorIdx = hw_random8();
      spotlights[i].width = hw_random8(1, 10);

      spotlights[i].speed = 1.0 / hw_random8(4, 50);

      if (initialize) {
        spotlights[i].position = hw_random16(seg_len);
        spotlights[i].speed *= hw_random8(2) ? 1.0 : -1.0;
      } else {
        if (hw_random8(2)) {
          spotlights[i].position = seg_len + spotlights[i].width;
          spotlights[i].speed *= -1.0;
        } else {
          spotlights[i].position = -spotlights[i].width;
        }
      }

      spotlights[i].lastUpdateTime = time;
      spotlights[i].type = hw_random8(SPOT_TYPES_COUNT);
    }

    uint32_t color = seg.color_from_palette(spotlights[i].colorIdx, false, false, 255);
    int start = spotlights[i].position;

    if (spotlights[i].width <= 1) {
      if (start >= 0 && start < (int) seg_len) {
        seg.blend_pixel_color(start, color, 128);
      }
    } else {
      switch (spotlights[i].type) {
        case SPOT_TYPE_SOLID:
          for (size_t j = 0; j < spotlights[i].width; j++) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, 128);
            }
          }
          break;

        case SPOT_TYPE_GRADIENT:
          for (size_t j = 0; j < spotlights[i].width; j++) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, cubicwave8(wf_map(j, 0, spotlights[i].width - 1, 0, 255)));
            }
          }
          break;

        case SPOT_TYPE_2X_GRADIENT:
          for (size_t j = 0; j < spotlights[i].width; j++) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, cubicwave8(2 * wf_map(j, 0, spotlights[i].width - 1, 0, 255)));
            }
          }
          break;

        case SPOT_TYPE_2X_DOT:
          for (size_t j = 0; j < spotlights[i].width; j += 2) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, 128);
            }
          }
          break;

        case SPOT_TYPE_3X_DOT:
          for (size_t j = 0; j < spotlights[i].width; j += 3) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, 128);
            }
          }
          break;

        case SPOT_TYPE_4X_DOT:
          for (size_t j = 0; j < spotlights[i].width; j += 4) {
            if ((start + j) >= 0 && (start + j) < seg_len) {
              seg.blend_pixel_color(start + j, color, 128);
            }
          }
          break;
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLENDS
/*
  Blends random colors across palette
  Modified, originally by Mark Kriegsman https://gist.github.com/kriegsman/1f7ccbbfa492a73c015e
*/
void mode_blends(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned pixelLen = seg_len > UINT8_MAX ? UINT8_MAX : seg_len;
  unsigned dataSize = sizeof(uint32_t) * (pixelLen + 1);  // max segment length of 56 pixels on 16 segment ESP8266
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  uint32_t *pixels = reinterpret_cast<uint32_t *>(seg.data);
  uint8_t blendSpeed = wf_map(seg.intensity, 0, UINT8_MAX, 10, 128);
  unsigned shift = (seg.now * ((seg.speed >> 3) + 1)) >> 8;

  for (unsigned i = 0; i < pixelLen; i++) {
    pixels[i] = color_blend(
        pixels[i], seg.color_from_palette(shift + quadwave8((i + 1) * 16), false, seg.palette_solid_wrap(), 255),
        blendSpeed);
    shift += 3;
  }

  unsigned offset = 0;
  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, pixels[offset++]);
    if (offset >= pixelLen)
      offset = 0;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_AURORA
/*
  Aurora effect
  original by @aggrosemilia, improved and converted to integer math by @dedehai
*/

// CONFIG
constexpr int W_MAX_COUNT = 20;   // Number of simultaneous waves
constexpr int W_MAX_SPEED = 6;    // Higher number, higher speed
constexpr int W_WIDTH_FACTOR = 6; // Higher number, smaller waves

// fixed-point math scaling
constexpr uint32_t AW_SHIFT = 16;
constexpr uint32_t AW_SCALE = (1u << AW_SHIFT);  // 65536 representing 1.0

// 32 bytes
class AuroraWave {
 private:
  int32_t center;             // scaled by AW_SCALE
  uint32_t ageFactor_cached;  // cached age factor scaled by AW_SCALE
  uint16_t ttl;
  uint16_t age;
  uint16_t width;
  uint16_t basealpha;     // scaled by AW_SCALE
  uint16_t speed_factor;  // scaled by AW_SCALE
  int16_t wave_start;     // wave start LED index
  int16_t wave_end;       // wave end LED index
  bool goingleft;
  bool alive = true;
  CRGBW basecolor;

 public:
  void init(uint32_t segment_length, CRGBW color) {
    ttl = hw_random16(500, 1501);
    basecolor = color;
    basealpha = hw_random8(60, 100) * AW_SCALE / 100;  // 0-99% note: if using 100% there is risk of integer overflow
    age = 0;
    width = hw_random16(segment_length / 20, segment_length / W_WIDTH_FACTOR) + 1;
    center = (((uint32_t) hw_random8(101) << AW_SHIFT) / 100) * segment_length;  // 0-100%
    goingleft = hw_random8() & 0x01;                                             // 50/50 chance
    speed_factor = (((uint32_t) hw_random8(10, 31) * W_MAX_SPEED) << AW_SHIFT) / (100 * 255);
    alive = true;
  }

  void updateCachedValues() {
    uint32_t half_ttl = ttl >> 1;
    if (age < half_ttl) {
      ageFactor_cached = ((uint32_t) age << AW_SHIFT) / half_ttl;
    } else {
      ageFactor_cached = ((uint32_t) (ttl - age) << AW_SHIFT) / half_ttl;
    }
    if (ageFactor_cached >= AW_SCALE)
      ageFactor_cached = AW_SCALE - 1;  // prevent overflow

    uint32_t center_led = center >> AW_SHIFT;
    wave_start = (int16_t) center_led - (int16_t) width;
    wave_end = (int16_t) center_led + (int16_t) width;
  }

  CRGBW getColorForLED(int ledIndex) {
    // linear brightness falloff from center to edge of wave
    if (ledIndex < wave_start || ledIndex > wave_end)
      return 0;
    int32_t ledIndex_scaled = (int32_t) ledIndex << AW_SHIFT;
    int32_t offset = ledIndex_scaled - center;
    if (offset < 0)
      offset = -offset;
    uint32_t offsetFactor = offset / width;  // scaled by AW_SCALE
    if (offsetFactor > AW_SCALE)
      return 0;  // outside of wave
    uint32_t brightness_factor = (AW_SCALE - offsetFactor);
    brightness_factor = (brightness_factor * ageFactor_cached) >> AW_SHIFT;
    brightness_factor = (brightness_factor * basealpha) >> AW_SHIFT;

    CRGBW rgb;
    rgb.r = (basecolor.r * brightness_factor) >> AW_SHIFT;
    rgb.g = (basecolor.g * brightness_factor) >> AW_SHIFT;
    rgb.b = (basecolor.b * brightness_factor) >> AW_SHIFT;
    rgb.w = (basecolor.w * brightness_factor) >> AW_SHIFT;

    return rgb;
  };

  // Change position and age of wave
  // Determine if its still "alive"
  void update(uint32_t segment_length, uint32_t speed) {
    int32_t step = speed_factor * speed;
    center += goingleft ? -step : step;
    age++;

    if (age > ttl) {
      alive = false;
    } else {
      uint32_t width_scaled = (uint32_t) width << AW_SHIFT;
      uint32_t segment_length_scaled = segment_length << AW_SHIFT;

      if (goingleft) {
        if (center < -(int32_t) width_scaled) {
          alive = false;
        }
      } else {
        if (center > (int32_t) segment_length_scaled + (int32_t) width_scaled) {
          alive = false;
        }
      }
    }
  };

  bool stillAlive() { return alive; }
};

void mode_aurora(Segment &seg) {
  const unsigned seg_len = seg.length();
  AuroraWave *waves;
  seg.aux1 = wf_map(seg.intensity, 0, 255, 2, W_MAX_COUNT);  // aux1 = Wavecount
  if (!seg.allocate_data(sizeof(AuroraWave) * seg.aux1)) {
    FX_FALLBACK_STATIC;
  }
  waves = reinterpret_cast<AuroraWave *>(seg.data);

  // note: on first call, seg.data is zero -> all waves are dead and will be initialized
  for (int i = 0; i < seg.aux1; i++) {
    waves[i].update(seg_len, seg.speed);
    if (!(waves[i].stillAlive())) {
      waves[i].init(seg_len, seg.color_from_palette(hw_random8(), false, false, hw_random8(0, 3)));
    }
    waves[i].updateCachedValues();
  }

  uint8_t backlight = 0;  // note: original code used 1, with inverse gamma applied background would never be black
  if (seg.color(0))
    backlight++;
  if (seg.color(1))
    backlight++;
  if (seg.color(2))
    backlight++;
  backlight = gamma8inv(backlight);  // preserve backlight when using gamma correction

  for (unsigned i = 0; i < seg_len; i++) {
    CRGBW mixedRgb = CRGBW(backlight, backlight, backlight);

    for (int j = 0; j < seg.aux1; j++) {
      CRGBW rgb = waves[j].getColorForLED(i);
      mixedRgb = color_add(mixedRgb, rgb);  // sum all waves influencing this pixel
    }

    seg.set_pixel_color(i, mixedRgb);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FLOW_STRIPE
//////////////////////////////
//     Flow Stripe          //
//////////////////////////////
// By: ldirko  https://editor.soulmatelights.com/gallery/392-flow-led-stripe , modifed by: Andrew Tuline, fixed by
// @DedeHai
void mode_FlowStripe(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const int hl = seg_len * 10 / 13;
  uint8_t hue = seg.now / (seg.speed + 1);
  uint32_t t = seg.now / (seg.intensity / 8 + 1);

  for (unsigned i = 0; i < seg_len; i++) {
    int c = ((abs((int) i - hl) * 127) / hl);
    c = sin8_t(c);
    c = sin8_t(c / 2 + t);
    uint8_t b = sin8_t(c + t / 8);
    seg.set_pixel_color(i, seg.color_from_palette(b + hue, false, true, 3));
  }
}  // mode_FlowStripe()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SHIMMER
/*
  Shimmer effect: moves a gradient with optional modulators across the strip at a given interval, up to 60 seconds
  It can be used as an overlay to other effects or standalone
  by DedeHai (Damian Schneider), based on idea from @Charming-Lime (#4905)
*/
void mode_shimmer(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.allocate_data(sizeof(uint32_t))) {
    FX_FALLBACK_STATIC;
  }
  uint32_t *lastTime = reinterpret_cast<uint32_t *>(seg.data);

  uint32_t radius = (seg.custom1 * seg_len >> 7) + 1;            // [1, 2*seg_len+1] pixels
  uint32_t traversalDistance = (seg_len + 2 * radius) << 8;      // total subpixels to cross, 1 pixel = 256 subpixels
  uint32_t traversalTime = 200 + (255 - seg.speed) * 80;         // [200, 20600] ms
  uint32_t speed = ((traversalDistance << 5) / traversalTime);   // subpixels/512ms
  int32_t position = static_cast<int32_t>(seg.step);             // current position in subpixels
  uint16_t inputstate = (uint16_t(seg.intensity) << 8) | uint16_t(seg.custom1);  // current user input state

  // init
  if (seg.call == 0 || inputstate != seg.aux1) {
    position = -(radius << 8);
    seg.aux0 = 0;  // aux0 is pause timer
    *lastTime = seg.now;
    seg.aux1 = inputstate;  // save user input state
  }

  if (seg.speed) {
    uint32_t deltaTime = (seg.now - *lastTime) & 0x7F;  // clamp to 127ms to avoid overflows. note: speed*deltaTime can
                                                        // still overflow for segments > ~10k pixels
    *lastTime = seg.now;

    if (seg.aux0 > 0) {
      seg.aux0 = (seg.aux0 > deltaTime) ? seg.aux0 - deltaTime : 0;
    } else {
      // calculate movement step and update position
      int32_t step = 1 + ((speed * deltaTime) >> 5);  // subpixels moved this frame. note >>5 as speed is in
                                                      // subpixels/512ms
      position += step;
      int endposition = (seg_len + radius) << 8;
      if (position > endposition) {
        seg.aux0 = seg.intensity * 236;  // [0, 60180] ms pause
        if (seg.check3)
          seg.aux0 = hw_random(seg.aux0 + 1000);  // randomise interval, +1 second to affect low intensity values
        position = -(radius << 8);                // reset to start position (out of frame)
      }
      seg.step = (uint32_t) position;  // save back
    }

    if (seg.check2)
      position = (seg_len << 8) - position;  // invert position (and direction)
  } else {
    position = (seg_len << 7);  // at speed=0, make it static in the center (this enables to use modulators only)
  }

  for (int i = 0; i < (int) seg_len; i++) {
    uint32_t dist = abs(position - (i << 8));
    if (dist < (radius << 8)) {
      uint32_t color = seg.color_from_palette(i * 255 / seg_len, false, false, 0);
      uint8_t blend = dist / radius;  // linear gradient note: dist is in subpixels, radius in pixels, result is
                                      // [0, 255] since dist < radius*256
      if (seg.custom2) {
        uint8_t modVal;  // modulation value
        if (seg.check1) {
          modVal = (sin16_t((i * seg.custom2 << 6) + (seg.now * seg.custom3 << 5)) >> 8) + 128;  // sine modulation:
                                                                                                 // regular "Zebra"
                                                                                                 // stripes
        } else {
          modVal = perlin16((i * seg.custom2 << 7), seg.now * seg.custom3 << 5) >> 8;  // perlin noise modulation
        }
        color = color_fade(color, modVal, true);  // dim by modulator value
      }
      seg.set_pixel_color(i, color_blend(color, seg.color(1), blend));  // blend to background color
    } else {
      seg.set_pixel_color(i, seg.color(1));
    }
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER
    {"Theater@!,Gap size;!,!;!", mode_theater_chase},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_THEATER_RAINBOW
    {"Theater Rainbow@!,Gap size;,!;!", mode_theater_chase_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_2
    {"Chase 2@!,Width;!,!;!", mode_running_color},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RUNNING
    {"Running@!,Wave width;!,!;!", mode_running_lights},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SAW
    {"Saw@!,Width;!,!;!", mode_saw},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RUNNING_DUAL
    {"Running Dual@!,Wave width;L,!,R;!", mode_running_dual},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_FLASH
    {"Chase Flash@!;Bg,Fx;!", mode_chase_flash},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHASE_FLASH_RND
    {"Chase Flash Rnd@!;!,!;!", mode_chase_flash_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RANDOM_COLORS
    {"Random Colors@!,Fade time;;!;01", mode_random_color},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PACIFICA
    {"Pacifica@!,Angle;;!;;pal=51", mode_pacifica},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_2
    {"Noise 2@!;!;!;;pal=43", mode_noise16_2},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_3
    {"Noise 3@!;!;!;;pal=35", mode_noise16_3},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_4
    {"Noise 4@!;!;!;;pal=26", mode_noise16_4},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FILL_NOISE
    {"Fill Noise@!;!;!", mode_fillnoise8},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE_PAL
    {"Noise Pal@!,Scale;;!", mode_noisepal},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE
    {"Sparkle@!,,,,,,Overlay;!,!;!;;m12=0", mode_sparkle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE_DARK
    {"Sparkle Dark@!,!,,,,,Overlay;Bg,Fx;!;;m12=0", mode_flash_sparkle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPARKLE_PLUS
    {"Sparkle+@!,!,,,,,Overlay;Bg,Fx;!;;m12=0", mode_hyper_sparkle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DANCING_SHADOWS
    {"Dancing Shadows@!,# of shadows;!;!", mode_dancing_shadows},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SHIMMER
    {"Shimmer@Speed,Interval,Size,Granular,Flow,Zebra,Reverse,Sporadic;Fx,Bg,Cx;!;1;pal=15,sx=220,ix=10,c2=0,c3=0",
     mode_shimmer},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POPCORN
    {"Popcorn@!,!,,,,,Overlay;!,!,!;!;;m12=1", mode_popcorn},  // bar
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORFUL
    {"Colorful@!,Saturation;1,2,3;!", mode_colorful},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_AURORA
    {"Aurora@!,!;1,2,3;!;;sx=24,pal=50", mode_aurora},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRI_WIPE
    {"Tri Wipe@!;1,2,3;!", mode_tricolor_wipe},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FLOW
    {"Flow@!,Zones;;!;;m12=1", mode_flow},  // vertical
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAILWAY
    {"Railway@!,Smoothness;1,2;!;;pal=3", mode_railway},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE_MEGA
    {"Strobe Mega@!,!;!,!;!;01", mode_multi_strobe},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLENDS
    {"Blends@Shift speed,Blend speed;;!", mode_blends},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BREATHE
    {"Breathe@!;!,!;!;01", mode_breath},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FLOW_STRIPE
    {"Flow Stripe@Hue speed,Effect speed;;!;pal=11", mode_FlowStripe},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_D;
const EffectGroup EFFECT_GROUP_1D_D{"1d_d", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_D
