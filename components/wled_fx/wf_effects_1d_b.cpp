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

/* 1D batch B. See BATCHES.md for the effect list this file owns. */

#include <algorithm>

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_1D_B                                                                            \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE || WLED_FX_FX_WIPE_RANDOM || WLED_FX_FX_SWEEP ||         \
   WLED_FX_FX_SWEEP_RANDOM || WLED_FX_FX_STROBE || WLED_FX_FX_STROBE_RAINBOW ||                       \
   WLED_FX_FX_BLINK_RAINBOW || WLED_FX_FX_TWINKLE || WLED_FX_FX_TWINKLEUP ||                          \
   WLED_FX_FX_FAIRYTWINKLE || WLED_FX_FX_COLORTWINKLES || WLED_FX_FX_TWINKLEFOX ||                    \
   WLED_FX_FX_TWINKLECAT || WLED_FX_FX_GRADIENT || WLED_FX_FX_LOADING || WLED_FX_FX_GLITTER ||        \
   WLED_FX_FX_SOLID_GLITTER || WLED_FX_FX_DYNAMIC || WLED_FX_FX_DYNAMIC_SMOOTH ||                     \
   WLED_FX_FX_ROLLING_BALLS || WLED_FX_FX_TETRIX || WLED_FX_FX_ICU || WLED_FX_FX_OSCILLATE ||         \
   WLED_FX_FX_SUNRISE || WLED_FX_FX_STREAM || WLED_FX_FX_SOLID_PATTERN_TRI || WLED_FX_FX_CHUNCHUN ||  \
   WLED_FX_FX_SINE || WLED_FX_FX_PERLIN_MOVE || WLED_FX_FX_WAVESINS)

#if WLED_FX_GROUP_1D_B

namespace esphome {
namespace wled_fx {
namespace {

/* ULTRAWHITE, FRAMETIME_FIXED, NUM_COLORS, get_random_wheel_index(), the shared
 * PRNG, speed_formula_l(), blink() and the Flasher struct all live in the engine
 * now, in wf_color.h, wf_segment.h and wf_fx_shared.h. */

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLINK_RAINBOW
/*
 * Classic Blink effect. Cycling through the rainbow.
 */
void mode_blink_rainbow(Segment &seg) { blink(seg, seg.color_wheel(seg.call & 0xFF), seg.color(1), false, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE
/*
 * Classic Strobe effect.
 */
void mode_strobe(Segment &seg) { return blink(seg, seg.color(0), seg.color(1), true, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE_RAINBOW
/*
 * Classic Strobe effect. Cycling through the rainbow.
 */
void mode_strobe_rainbow(Segment &seg) {
  return blink(seg, seg.color_wheel(seg.call & 0xFF), seg.color(1), true, false);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE || WLED_FX_FX_SWEEP || WLED_FX_FX_WIPE_RANDOM || \
    WLED_FX_FX_SWEEP_RANDOM
/*
 * Color wipe function
 * LEDs are turned on (color1) in sequence, then turned off (color2) in sequence.
 * if (bool rev == true) then LEDs are turned off in reverse order
 */
void color_wipe(Segment &seg, bool rev, bool useRandomColors) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  uint32_t cycleTime = 750 + (255 - seg.speed) * 150;
  uint32_t perc = seg.now % cycleTime;
  unsigned prog = (perc * 65535) / cycleTime;
  bool back = (prog > 32767);
  if (back) {
    prog -= 32767;
    if (seg.step == 0)
      seg.step = 1;
  } else {
    if (seg.step == 2)
      seg.step = 3;  // trigger color change
  }

  if (useRandomColors) {
    if (seg.call == 0) {
      seg.aux0 = hw_random8();
      seg.step = 3;
    }
    if (seg.step == 1) {  // if flag set, change to new random color
      seg.aux1 = get_random_wheel_index(seg.aux0);
      seg.step = 2;
    }
    if (seg.step == 3) {
      seg.aux0 = get_random_wheel_index(seg.aux1);
      seg.step = 0;
    }
  }

  unsigned ledIndex = (prog * seg_len) >> 15;
  uint16_t rem = (prog * seg_len) * 2;  // mod 0xFFFF by truncating
  rem /= (seg.intensity + 1);
  if (rem > 255)
    rem = 255;

  uint32_t col1 = useRandomColors ? seg.color_wheel(seg.aux1) : seg.color(1);
  for (unsigned i = 0; i < seg_len; i++) {
    unsigned index = (rev && back) ? seg_len - 1 - i : i;
    uint32_t col0 = useRandomColors ? seg.color_wheel(seg.aux0)
                                    : seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0);

    if (i < ledIndex) {
      seg.set_pixel_color(index, back ? col1 : col0);
    } else {
      seg.set_pixel_color(index, back ? col0 : col1);
      if (i == ledIndex)
        seg.set_pixel_color(index, color_blend(back ? col0 : col1, back ? col1 : col0, uint8_t(rem)));
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE
/*
 * Lights all LEDs one after another.
 */
void mode_color_wipe(Segment &seg) { color_wipe(seg, false, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWEEP
/*
 * Lights all LEDs one after another. Turns off opposite
 */
void mode_color_sweep(Segment &seg) { color_wipe(seg, true, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE_RANDOM
/*
 * Turns all LEDs after each other to a random color.
 * Then starts over with another color.
 */
void mode_color_wipe_random(Segment &seg) { color_wipe(seg, false, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWEEP_RANDOM
/*
 * Random color introduced alternating from start and end of strip.
 */
void mode_color_sweep_random(Segment &seg) { color_wipe(seg, true, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DYNAMIC || WLED_FX_FX_DYNAMIC_SMOOTH
/*
 * Lights every LED in a random color. Changes all LED at the same time
 * to new random colors.
 */
void mode_dynamic(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.allocate_data(seg_len))
    FX_FALLBACK_STATIC;  // allocation failed

  if (seg.call == 0) {
    // seg.fill(BLACK);
    for (unsigned i = 0; i < seg_len; i++)
      seg.data[i] = hw_random8();
  }

  uint32_t cycleTime = 50 + (255 - seg.speed) * 15;
  uint32_t it = seg.now / cycleTime;
  if (it != seg.step && seg.speed != 0)  // new color
  {
    for (unsigned i = 0; i < seg_len; i++) {
      if (hw_random8() <= seg.intensity)
        seg.data[i] = hw_random8();  // random color index
    }
    seg.step = it;
  }

  if (seg.check1) {
    for (unsigned i = 0; i < seg_len; i++) {
      seg.blend_pixel_color(i, seg.color_wheel(seg.data[i]), 16);
    }
  } else {
    for (unsigned i = 0; i < seg_len; i++) {
      seg.set_pixel_color(i, seg.color_wheel(seg.data[i]));
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DYNAMIC_SMOOTH
/*
 * effect "Dynamic" with smooth color-fading
 */
void mode_dynamic_smooth(Segment &seg) {
  bool old = seg.check1;
  seg.check1 = true;
  mode_dynamic(seg);
  seg.check1 = old;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE
/*
 * Blink several LEDs in random colors on, reset, repeat.
 * Inspired by www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/
 */
void mode_twinkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  seg.fade_out(224);

  uint32_t cycleTime = 20 + (255 - seg.speed) * 5;
  uint32_t it = seg.now / cycleTime;
  if (it != seg.step) {
    unsigned maxOn = wf_map(seg.intensity, 0, 255, 1, seg_len);  // make sure at least one LED is on
    if (seg.aux0 >= maxOn) {
      seg.aux0 = 0;
      seg.aux1 = hw_random();  // new seed for our PRNG
    }
    seg.aux0++;
    seg.step = it;
  }

  uint16_t PRNG16 = seg.aux1;

  for (unsigned i = 0; i < seg.aux0; i++) {
    PRNG16 = (uint16_t) (PRNG16 * 2053) + 13849;  // next 'random' number
    uint32_t p = (uint32_t) seg_len * (uint32_t) PRNG16;
    unsigned j = p >> 16;
    seg.set_pixel_color(j, seg.color_from_palette(j, true, seg.palette_solid_wrap(), 0));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STREAM
/*
 * Random colored pixels running. ("Stream")
 */
void mode_running_random(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = 25 + (3 * (uint32_t) (255 - seg.speed));
  uint32_t it = seg.now / cycleTime;
  if (seg.call == 0)
    seg.aux0 = hw_random();  // random seed for PRNG on start

  unsigned zoneSize = ((255 - seg.intensity) >> 4) + 1;
  uint16_t PRNG16 = seg.aux0;

  unsigned z = it % zoneSize;
  bool nzone = (!z && it != seg.aux1);
  for (int i = seg_len - 1; i >= 0; i--) {
    if (nzone || z >= zoneSize) {
      unsigned lastrand = PRNG16 >> 8;
      int16_t diff = 0;
      while (abs(diff) < 42) {                         // make sure the difference between adjacent colors is big enough
        PRNG16 = (uint16_t) (PRNG16 * 2053) + 13849;   // next zone, next 'random' number
        diff = (PRNG16 >> 8) - lastrand;
      }
      if (nzone) {
        seg.aux0 = PRNG16;  // save next starting seed
        nzone = false;
      }
      z = 0;
    }
    seg.set_pixel_color(i, seg.color_wheel(PRNG16 >> 8));
    z++;
  }

  seg.aux1 = it;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRADIENT || WLED_FX_FX_LOADING
/*
 * Gradient run base function
 */
void gradient_base(Segment &seg, bool loading) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  uint16_t counter = seg.now * ((seg.speed >> 2) + 1);
  uint16_t pp = (counter * seg_len) >> 16;
  if (seg.call == 0)
    pp = 0;
  int val;  // 0 = sec 1 = pri
  int brd = 1 + loading ? seg.intensity / 2 : seg.intensity / 4;
  // if (brd < 1) brd = 1;
  int p1 = pp - seg_len;
  int p2 = pp + seg_len;

  for (int i = 0; i < (int) seg_len; i++) {
    if (loading) {
      val = abs(((i > pp) ? p2 : pp) - i);
    } else {
      val = std::min(abs(pp - i), std::min(abs(p1 - i), abs(p2 - i)));
    }
    val = (brd > val) ? (val * 255) / brd : 255;
    seg.set_pixel_color(i, color_blend(seg.color(0), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1),
                                       uint8_t(val)));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRADIENT
/*
 * Gradient run
 */
void mode_gradient(Segment &seg) { gradient_base(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LOADING
/*
 * Gradient run with hard transition
 */
void mode_loading(Segment &seg) { gradient_base(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FAIRYTWINKLE
/*
 * Fairytwinkle. Like Colortwinkle, but starting from all lit and not relying on strip.getPixelColor
 * Warning: Uses 4 bytes of segment data per pixel
 */
void mode_fairytwinkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned dataSize = sizeof(flasher) * seg_len;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  Flasher *flashers = reinterpret_cast<Flasher *>(seg.data);
  unsigned now16 = seg.now & 0xFFFF;
  uint16_t PRNG16 = 5100 + 0;

  unsigned riseFallTime = 400 + (255 - seg.speed) * 3;
  unsigned maxDur = riseFallTime / 100 + ((255 - seg.intensity) >> 2) + 13 + ((255 - seg.intensity) >> 1);

  for (unsigned f = 0; f < seg_len; f++) {
    uint16_t stateTime = now16 - flashers[f].stateStart;
    // random on/off time reached, switch state
    if (stateTime > flashers[f].stateDur * 100) {
      flashers[f].stateOn = !flashers[f].stateOn;
      bool init = !flashers[f].stateDur;
      if (flashers[f].stateOn) {
        flashers[f].stateDur =
            riseFallTime / 100 + ((255 - seg.intensity) >> 2) + hw_random8(12 + ((255 - seg.intensity) >> 1)) + 1;
      } else {
        flashers[f].stateDur = riseFallTime / 100 + hw_random8(3 + ((255 - seg.speed) >> 6)) + 1;
      }
      flashers[f].stateStart = now16;
      stateTime = 0;
      if (init) {
        flashers[f].stateStart -= riseFallTime;  // start lit
        flashers[f].stateDur =
            riseFallTime / 100 + hw_random8(12 + ((255 - seg.intensity) >> 1)) + 5;  // fire up a little quicker
        stateTime = riseFallTime;
      }
    }
    if (flashers[f].stateOn && flashers[f].stateDur > maxDur)
      flashers[f].stateDur = maxDur;  // react more quickly on intensity change
    if (stateTime > riseFallTime)
      stateTime = riseFallTime;  // for flasher brightness calculation, fades in first 255 ms of state
    unsigned fadeprog = 255 - ((stateTime * 255) / riseFallTime);
    uint8_t flasherBri = (flashers[f].stateOn) ? 255 - gamma8(fadeprog) : gamma8(fadeprog);
    unsigned lastR = PRNG16;
    unsigned diff = 0;
    while (diff < 0x4000) {                          // make sure colors of two adjacent LEDs differ enough
      PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;    // next 'random' number
      diff = (PRNG16 > lastR) ? PRNG16 - lastR : lastR - PRNG16;
    }
    seg.set_pixel_color(f, color_blend(seg.color(1), seg.color_from_palette(PRNG16 >> 8, false, false, 0), flasherBri));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ICU
/*
 * ICU mode
 */
void mode_icu(Segment &seg) {
  // states: 0 = pause1, 1 = blink, 2 = pause2, 3 = move
  const unsigned seg_len = seg.length();

  uint16_t now = seg.now;  // save time for delay calculation, use low16 bits only
  unsigned dest = seg.aux1;
  unsigned space = (seg.intensity >> 3) + 2;
  uint16_t state = seg.step >> 16;          // upper bytes of step store current state
  uint16_t nextUpdate = seg.step & 0xFFFF;  // lower bytes store time for next update

  uint8_t pindex = wf_map(dest, 0, seg_len - seg_len / space, 0, 255);
  uint32_t col = seg.color_from_palette(pindex, false, false, 0);
  uint32_t bgcol = seg.check2 ? BLACK : seg.color(1);
  seg.fill(bgcol);  // apply background color or clear
  // draw eyes if not blinking
  if (state != 1) {
    seg.set_pixel_color(dest, col);
    seg.set_pixel_color(dest + seg_len / space, col);
    // render next position if moving
    if (state == 3) {
      if (seg.aux0 > seg.aux1) {
        dest++;
      } else if (seg.aux0 < seg.aux1) {
        dest--;
      }
      seg.set_pixel_color(dest, col);
      seg.set_pixel_color(dest + seg_len / space, col);
    }
  }

  // update state
  if ((int16_t) (now - nextUpdate) >= 0) {  // time to update, cast to int to handle wraparound properly
    switch (state) {
      case 0:  // pause part 1
        // first pause part finished, blink or pause some more
        state++;
        if (hw_random8(6) == 0) {  // blink once in a while
          nextUpdate = uint16_t(now + 200);
          break;
        }
        // fall through if not blinking
        [[fallthrough]];
      case 1:  // blink
        // not blinking or finished blinking -> pause part 2
        nextUpdate = uint16_t(now + 500 + hw_random16(1000));
        state++;
        break;
      case 2:  // pause part 2
        // pause finished, move
        seg.aux0 = hw_random16(seg_len - seg_len / space);  // choose a new destination
        nextUpdate = now;
        state++;
        break;
      default:  // move (state 3)
        seg.aux1 = dest;  // update destination to moved position
        nextUpdate = uint16_t(now + speed_formula_l(seg, seg_len));
        if (seg.aux0 == dest) {
          // reached destination
          nextUpdate = uint16_t(now + 500 + hw_random16(1000));
          state = 0;
        }
        break;
    }
  }

  // use upper bits of seg.step to store current state, lower bits for next update time
  seg.step = (state << 16) | nextUpdate;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_OSCILLATE
// 7 bytes
typedef struct Oscillator {
  uint16_t pos;
  uint8_t size;
  int8_t dir;
  uint8_t speed;
} oscillator;

/*
/  Oscillating bars of color, updated with standard framerate
*/
void mode_oscillate(Segment &seg) {
  const unsigned seg_len = seg.length();
  constexpr unsigned numOscillators = 3;
  constexpr unsigned dataSize = sizeof(oscillator) * numOscillators;

  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  Oscillator *oscillators = reinterpret_cast<Oscillator *>(seg.data);

  if (seg.call == 0) {
    oscillators[0] = {(uint16_t) (seg_len / 4), (uint8_t) (seg_len / 8), 1, 1};
    oscillators[1] = {(uint16_t) (seg_len / 4 * 3), (uint8_t) (seg_len / 8), 1, 2};
    oscillators[2] = {(uint16_t) (seg_len / 4 * 2), (uint8_t) (seg_len / 8), -1, 1};
  }

  uint32_t cycleTime = 20 + (2 * (uint32_t) (255 - seg.speed));
  uint32_t it = seg.now / cycleTime;

  for (unsigned i = 0; i < numOscillators; i++) {
    // if the counter has increased, move the oscillator by the random step
    if (it != seg.step)
      oscillators[i].pos += oscillators[i].dir * oscillators[i].speed;
    oscillators[i].size = seg_len / (3 + seg.intensity / 8);
    if ((oscillators[i].dir == -1) && (oscillators[i].pos > seg_len << 1)) {  // use integer overflow
      oscillators[i].pos = 0;
      oscillators[i].dir = 1;
      // make bigger steps for faster speeds
      oscillators[i].speed = seg.speed > 100 ? hw_random8(2, 4) : hw_random8(1, 3);
    }
    if ((oscillators[i].dir == 1) && (oscillators[i].pos >= (seg_len - 1))) {
      oscillators[i].pos = seg_len - 1;
      oscillators[i].dir = -1;
      oscillators[i].speed = seg.speed > 100 ? hw_random8(2, 4) : hw_random8(1, 3);
    }
  }

  for (unsigned i = 0; i < seg_len; i++) {
    uint32_t color = BLACK;
    for (unsigned j = 0; j < numOscillators; j++) {
      if ((int) i >= (int) oscillators[j].pos - oscillators[j].size &&
          i <= unsigned(oscillators[j].pos + oscillators[j].size)) {
        color = (color == BLACK) ? seg.color(j) : color_blend(color, seg.color(j), uint8_t(128));
      }
    }
    seg.set_pixel_color(i, color);
  }

  seg.step = it;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORTWINKLES
// based on https://gist.github.com/kriegsman/5408ecd397744ba0393e
void mode_colortwinkle(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned dataSize = (seg_len + 7) >> 3;  // 1 bit per LED
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  // limit update rate
  if (seg.now - seg.step < FRAMETIME_FIXED)
    return;
  seg.step = seg.now;

  CRGBW col, prev;
  // strip.getBrightness() is always 255 here: the light front end owns brightness.
  uint8_t fadeUpAmount = 8 + (seg.speed >> 2);
  uint8_t fadeDownAmount = 8 + (seg.speed >> 3);
  for (unsigned i = 0; i < seg_len; i++) {
    CRGBW cur = seg.get_pixel_color(i);
    prev = cur;
    unsigned index = i >> 3;
    unsigned bitNum = i & 0x07;
    bool fadeUp = seg.data[index] & (1 << bitNum);

    if (fadeUp) {
      CRGBW incrementalColor = color_fade(cur, fadeUpAmount, true);
      col = color_add(cur, incrementalColor);

      if (col.r == 255 || col.g == 255 || col.b == 255) {
        seg.data[index] &= ~(1 << bitNum);
      }

      if (col == cur) {  // color_add did nothing, fix "stuck" pixels by adding the color to itself
        col = color_add(col, col);
      }
      seg.set_pixel_color(i, col);
    } else {
      col = color_fade(cur, 255 - fadeDownAmount, false);
      seg.set_pixel_color(i, col);
    }
  }

  for (unsigned j = 0; j <= seg_len / 50; j++) {
    if (hw_random8() <= seg.intensity) {
      for (unsigned times = 0; times < 5; times++) {  // attempt to spawn a new pixel 5 times
        int i = hw_random16(seg_len);
        if (seg.get_pixel_color(i) == 0) {
          unsigned index = i >> 3;
          unsigned bitNum = i & 0x07;
          seg.data[index] |= (1 << bitNum);
          seg.set_pixel_color(
              i, ColorFromPalette(seg.palette_ref(), hw_random8(), gamma8inv(64),
                                  NOBLEND));  // note on gamma8inv: inverting results in non-linear brightness fade as
                                              // originally designed
          break;  // only spawn 1 new pixel per frame per 50 LEDs
        }
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLEFOX || WLED_FX_FX_TWINKLECAT
//  TwinkleFOX by Mark Kriegsman: https://gist.github.com/kriegsman/756ea6dcae8e30845b5a
//
//  TwinkleFOX: Twinkling 'holiday' lights that fade in and out.
//  Colors are chosen from a palette. Read more about this effect using the link above!
CRGBW twinklefox_one_twinkle(Segment &seg, uint32_t ms, uint8_t salt, bool cat) {
  // Overall twinkle speed (changed)
  unsigned ticks = ms / seg.aux0;
  unsigned fastcycle8 = uint8_t(ticks);
  uint16_t slowcycle16 = (ticks >> 8) + salt;
  slowcycle16 += sin8_t(slowcycle16);
  slowcycle16 = (slowcycle16 * 2053) + 1384;
  uint8_t slowcycle8 = (slowcycle16 & 0xFF) + (slowcycle16 >> 8);

  // Overall twinkle density.
  // 0 (NONE lit) to 8 (ALL lit at once).
  // Default is 5.
  unsigned twinkleDensity = (seg.intensity >> 5) + 1;

  unsigned bright = 0;
  if (((slowcycle8 & 0x0E) / 2) < twinkleDensity) {
    unsigned ph = fastcycle8;
    // This is like 'triwave8', which produces a
    // symmetrical up-and-down triangle sawtooth waveform, except that this
    // function produces a triangle wave with a faster attack and a slower decay
    if (cat) {  // twinklecat, variant where the leds instantly turn on and fade off
      bright = 255 - ph;
      if (seg.check2) {  // reverse checkbox, reverses the leds to fade on and instantly turn off
        bright = ph;
      }
    } else {  // vanilla twinklefox
      if (ph < 86) {
        bright = ph * 3;
      } else {
        ph -= 86;
        bright = 255 - (ph + (ph / 2));
      }
    }
  }

  unsigned hue = slowcycle8 - salt;
  CRGBW c;
  if (bright > 0) {
    c = ColorFromPalette(seg.palette_ref(), hue, gamma8inv(bright),
                         NOBLEND);  // note on gamma8inv: inverting results in non-linear brightness fade as originally
                                    // designed
    if (!seg.check1) {
      // This code takes a pixel, and if its in the 'fading down'
      // part of the cycle, it adjusts the color a little bit like the
      // way that incandescent bulbs fade toward 'red' as they dim.
      if (fastcycle8 >= 128) {
        unsigned cooling = (fastcycle8 - 128) >> 4;
        c.g = qsub8(c.g, cooling);
        c.b = qsub8(c.b, cooling * 2);
      }
    }
  } else {
    c = 0;  // black
  }
  return c;
}

//  This function loops over each pixel, calculates the
//  adjusted 'clock' that this pixel should use, and calls
//  "CalculateOneTwinkle" on each pixel.  It then displays
//  either the twinkle color of the background color,
//  whichever is brighter.
void twinklefox_base(Segment &seg, bool cat) {
  const unsigned seg_len = seg.length();
  // "PRNG16" is the pseudorandom number generator
  // It MUST be reset to the same starting value each time
  // this function is called, so that the sequence of 'random'
  // numbers that it generates is (paradoxically) stable.
  uint16_t PRNG16 = 11337;

  // Calculate speed
  if (seg.speed > 100)
    seg.aux0 = 3 + ((255 - seg.speed) >> 3);
  else
    seg.aux0 = 22 + ((100 - seg.speed) >> 1);

  // Set up the background color, "bg". Note: using gamma invert for brightness as the FX was written without any gamma
  // correction, it will dim down too much now
  CRGBW bg = seg.color(1);
  unsigned bglight = bg.getRGBaverage();
  if (bglight > 64) {
    bg = color_fade(bg, gamma8inv(16), true);  // very bright, so scale to 1/16th
  } else if (bglight > 16) {
    bg = color_fade(bg, gamma8inv(64), true);  // not that bright, so scale to 1/4
  } else {
    bg = color_fade(bg, gamma8inv(86), true);  // dim, scale to 1/3rd
  }

  bglight = bg.getRGBaverage();  // update after scaling

  for (unsigned i = 0; i < seg_len; i++) {
    PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;  // next 'random' number
    unsigned myclockoffset16 = PRNG16;           // use that number as clock offset
    PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;  // next 'random' number
    // use that number as clock speed adjustment factor (in 8ths, from 8/8ths to 23/8ths)
    unsigned myspeedmultiplierQ5_3 = ((((PRNG16 & 0xFF) >> 4) + (PRNG16 & 0x0F)) & 0x0F) + 0x08;
    uint32_t myclock30 = (uint32_t) ((seg.now * myspeedmultiplierQ5_3) >> 3) + myclockoffset16;
    unsigned myunique8 = PRNG16 >> 8;  // get 'salt' value for this pixel

    // We now have the adjusted 'clock' for this pixel, now we call
    // the function that computes what color the pixel should be based
    // on the "brightness = f( time )" idea.
    CRGBW c = twinklefox_one_twinkle(seg, myclock30, myunique8, cat);

    unsigned cbright = c.getRGBaverage();
    int deltabright = cbright - bglight;
    if (deltabright >= 32 || (bg == 0)) {
      // If the new pixel is significantly brighter than the background color,
      // use the new color.
      seg.set_pixel_color(i, c);
    } else if (deltabright > 0) {
      // If the new pixel is just slightly brighter than the background color,
      // mix a blend of the new color and the background color
      seg.set_pixel_color(i, color_blend(bg, c, uint8_t(deltabright * 8)));
    } else {
      // if the new pixel is not at all brighter than the background color,
      // just use the background color.
      seg.set_pixel_color(i, bg);
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLEFOX
void mode_twinklefox(Segment &seg) { twinklefox_base(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLECAT
void mode_twinklecat(Segment &seg) { twinklefox_base(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_PATTERN_TRI
void mode_tri_static_pattern(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned segSize = (seg.intensity >> 5) + 1;
  unsigned currSeg = 0;
  unsigned currSegCount = 0;

  for (unsigned i = 0; i < seg_len; i++) {
    if (currSeg % 3 == 0) {
      seg.set_pixel_color(i, seg.color(0));
    } else if (currSeg % 3 == 1) {
      seg.set_pixel_color(i, seg.color(1));
    } else {
      seg.set_pixel_color(i, seg.color(2));
    }
    currSegCount += 1;
    if (currSegCount >= segSize) {
      currSeg += 1;
      currSegCount = 0;
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROLLING_BALLS
typedef struct RollingBall {
  unsigned long lastBounceUpdate;
  float mass;  // could fix this to be = 1. if memory is an issue
  float velocity;
  float height;
} rball_t;

void mode_rolling_balls(Segment &seg) {
  const unsigned seg_len = seg.length();
  // allocate segment data
  const unsigned maxNumBalls = 16;  // 255/16 + 1
  unsigned dataSize = sizeof(rball_t) * maxNumBalls;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  rball_t *balls = reinterpret_cast<rball_t *>(seg.data);

  // number of balls based on intensity setting to max of 16 (cycles colors)
  // non-chosen color is a random color
  unsigned numBalls = seg.intensity / 16 + 1;
  bool hasCol2 = seg.color(2);

  if (seg.call == 0) {
    seg.fill(hasCol2 ? BLACK : seg.color(1));  // start clean
    for (unsigned i = 0; i < maxNumBalls; i++) {
      balls[i].lastBounceUpdate = seg.now;
      balls[i].velocity = 20.0f * float(hw_random16(1000, 10000)) / 10000.0f;  // number from 1 to 10
      if (hw_random8() < 128)
        balls[i].velocity = -balls[i].velocity;                         // 50% chance of reverse direction
      balls[i].height = (float(hw_random16(0, 10000)) / 10000.0f);      // from 0. to 1.
      balls[i].mass = (float(hw_random16(1000, 10000)) / 10000.0f);     // from .1 to 1.
    }
  }

  float cfac = float(scale8(8, 255 - seg.speed) + 1) *
               20000.0f;  // this uses the Aircoookie conversion factor for scaling time using speed slider

  if (seg.check3)
    seg.fade_out(250);  // 2-8 pixel trails (optional)
  else {
    if (!seg.check2)
      seg.fill(hasCol2 ? BLACK : seg.color(1));  // don't fill with background color if user wants to see trails
  }

  for (unsigned i = 0; i < numBalls; i++) {
    float timeSinceLastUpdate = float((seg.now - balls[i].lastBounceUpdate)) / cfac;
    float thisHeight =
        balls[i].height + balls[i].velocity * timeSinceLastUpdate;  // this method keeps higher resolution
    // test if intensity level was increased and some balls are way off the track then put them back
    if (thisHeight < -0.5f || thisHeight > 1.5f) {
      thisHeight = balls[i].height = (float(hw_random16(0, 10000)) / 10000.0f);  // from 0. to 1.
      balls[i].lastBounceUpdate = seg.now;
    }
    // check if reached ends of the strip
    if ((thisHeight <= 0.0f && balls[i].velocity < 0.0f) || (thisHeight >= 1.0f && balls[i].velocity > 0.0f)) {
      balls[i].velocity = -balls[i].velocity;  // reverse velocity
      balls[i].lastBounceUpdate = seg.now;
      balls[i].height = thisHeight;
    }
    // check for collisions
    if (seg.check1) {
      for (unsigned j = i + 1; j < numBalls; j++) {
        if (balls[j].velocity != balls[i].velocity) {
          //  tcollided + balls[j].lastBounceUpdate is acutal time of collision (this keeps precision with long to float
          //  conversions)
          float tcollided = (cfac * (balls[i].height - balls[j].height) +
                             balls[i].velocity * float(balls[j].lastBounceUpdate - balls[i].lastBounceUpdate)) /
                            (balls[j].velocity - balls[i].velocity);

          if ((tcollided > 2.0f) &&
              (tcollided < float(seg.now - balls[j].lastBounceUpdate))) {  // 2ms minimum to avoid duplicate bounces
            balls[i].height = balls[i].height + balls[i].velocity *
                                                    (tcollided + float(balls[j].lastBounceUpdate -
                                                                       balls[i].lastBounceUpdate)) /
                                                    cfac;
            balls[j].height = balls[i].height;
            balls[i].lastBounceUpdate = (unsigned long) (tcollided + 0.5f) + balls[j].lastBounceUpdate;
            balls[j].lastBounceUpdate = balls[i].lastBounceUpdate;
            float vtmp = balls[i].velocity;
            balls[i].velocity = ((balls[i].mass - balls[j].mass) * vtmp + 2.0f * balls[j].mass * balls[j].velocity) /
                                (balls[i].mass + balls[j].mass);
            balls[j].velocity = ((balls[j].mass - balls[i].mass) * balls[j].velocity + 2.0f * balls[i].mass * vtmp) /
                                (balls[i].mass + balls[j].mass);
            thisHeight = balls[i].height + balls[i].velocity * (seg.now - balls[i].lastBounceUpdate) / cfac;
          }
        }
      }
    }

    uint32_t color = seg.color(0);
    if (seg.palette) {
      // color = seg.color_wheel(i*(256/MAX(numBalls, 8)));
      color = seg.color_from_palette(i * 255 / numBalls, false, seg.palette_solid_wrap(), 0);
    } else if (hasCol2) {
      color = seg.color(i % NUM_COLORS);
    }

    if (thisHeight < 0.0f)
      thisHeight = 0.0f;
    if (thisHeight > 1.0f)
      thisHeight = 1.0f;
    unsigned pos = round(thisHeight * (seg_len - 1));
    seg.set_pixel_color(pos, color);
    balls[i].lastBounceUpdate = seg.now;
    balls[i].height = thisHeight;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GLITTER || WLED_FX_FX_SOLID_GLITTER
// utility function that will add random glitter to seg
void glitter_base(Segment &seg, uint8_t intensity, uint32_t col = ULTRAWHITE) {
  const unsigned seg_len = seg.length();
  if (intensity > hw_random8())
    seg.set_pixel_color(hw_random16(seg_len), col);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GLITTER
// Glitter with palette background, inspired by https://gist.github.com/kriegsman/062e10f7f07ba8518af6
void mode_glitter(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (!seg.check2) {  // use "* Color 1" palette for solid background (replacing "Solid glitter")
    unsigned counter = 0;
    if (seg.speed != 0) {
      counter = (seg.now * ((seg.speed >> 3) + 1)) & 0xFFFF;
      counter = counter >> 8;
    }

    bool noWrap = (seg.palette_blend == 2 || (seg.palette_blend == 0 && seg.speed == 0));
    for (unsigned i = 0; i < seg_len; i++) {
      unsigned colorIndex = (i * 255 / seg_len) - counter;
      if (noWrap)
        colorIndex = wf_map(colorIndex, 0, 255, 0, 240);  // cut off blend at palette "end"
      seg.set_pixel_color(i, seg.color_from_palette(colorIndex, false, true, 255));
    }
  }
  glitter_base(seg, seg.intensity, seg.color(2) ? seg.color(2) : ULTRAWHITE);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_GLITTER
// Solid colour background with glitter (can be replaced by Glitter)
void mode_solid_glitter(Segment &seg) {
  seg.fill(seg.color(0));
  glitter_base(seg, seg.intensity, seg.color(2) ? seg.color(2) : ULTRAWHITE);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TETRIX
// 20 bytes
typedef struct Tetris {
  float pos;
  float speed;
  uint8_t col;     // color index
  uint16_t brick;  // brick size in pixels
  uint16_t stack;  // stack size in pixels
  uint32_t step;   // 2D-fication of seg.step (state)
} tetris;

void mode_tetrix(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned strips = seg.nr_of_v_strips();  // allow running on virtual strips (columns in 2D segment)
  unsigned dataSize = sizeof(tetris);
  if (!seg.allocate_data(dataSize * strips))
    FX_FALLBACK_STATIC;  // allocation failed
  Tetris *drops = reinterpret_cast<Tetris *>(seg.data);

  // if (seg.call == 0) seg.fill(seg.color(1));  // will fill entire segment (1D or 2D), then use drop->step = 0 below

  // virtualStrip idea by @ewowi (Ewoud Wijma)
  // requires virtual strip # to be embedded into upper 16 bits of index in setPixelcolor()
  // the following functions will not work on virtual strips: fill(), fade_out(), fadeToBlack(), blur()
  const auto run_strip = [&](size_t stripNr, Tetris *drop) {
    // initialize dropping on first call or segment full
    if (seg.call == 0) {
      drop->stack = 0;                // reset brick stack size
      drop->step = seg.now + 2000;    // start by fading out strip
      if (seg.check1)
        drop->col = 0;                // use only one color from palette
    }

    if (drop->step == 0) {  // init brick
      // speed calculation: a single brick should reach bottom of strip in X seconds
      // if the speed is set to 1 this should take 5s and at 255 it should take 0.25s
      // as this is dependant on SEGLEN it should be taken into account and the fact that effect runs every FRAMETIME s
      int speed = seg.speed ? seg.speed : hw_random8(1, 255);
      speed = wf_map(speed, 1, 255, 5000, 250);               // time taken for full (SEGLEN) drop
      drop->speed = float(seg_len * FRAMETIME) / float(speed);  // set speed
      drop->pos = seg_len;                                    // start at end of segment (no need to subtract 1)
      if (!seg.check1)
        drop->col = hw_random8(0, 15) << 4;                   // limit color choices so there is enough HUE gap
      drop->step = 1;                                         // drop state (0 init, 1 forming, 2 falling)
      drop->brick =
          (seg.intensity ? (seg.intensity >> 5) + 1 : hw_random8(1, 5)) * (1 + (seg_len >> 6));  // size of brick
    }

    if (drop->step == 1) {      // forming
      if (hw_random8() >> 6) {  // random drop
        drop->step = 2;         // fall
      }
    }

    if (drop->step == 2) {            // falling
      if (drop->pos > drop->stack) {  // fall until top of stack
        drop->pos -= drop->speed;     // may add gravity as: speed += gravity
        if (int(drop->pos) < int(drop->stack))
          drop->pos = drop->stack;
        for (unsigned i = unsigned(drop->pos); i < seg_len; i++) {
          uint32_t col = i < unsigned(drop->pos) + drop->brick
                             ? seg.color_from_palette(drop->col, false, false, 0)
                             : seg.color(1);
          seg.set_pixel_color(Segment::index_to_v_strip(i, stripNr), col);
        }
      } else {                       // we hit bottom
        drop->step = 0;              // proceed with next brick, go back to init
        drop->stack += drop->brick;  // increase the stack size
        if (drop->stack >= seg_len)
          drop->step = seg.now + 2000;  // fade out stack
      }
    }

    if (drop->step > 2) {  // fade strip
      drop->brick = 0;     // reset brick size (no more growing)
      if (drop->step > seg.now) {
        // allow fading of virtual strip
        for (unsigned i = 0; i < seg_len; i++)
          seg.blend_pixel_color(Segment::index_to_v_strip(i, stripNr), seg.color(1), 25);  // 10% blend
      } else {
        drop->stack = 0;  // reset brick stack size
        drop->step = 0;   // proceed with next brick
        if (seg.check1)
          drop->col += 8;  // gradually increase palette index
      }
    }
  };

  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &drops[stripNr]);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SUNRISE
/*
 * Mode simulates a gradual sunrise
 */
void mode_sunrise(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // speed 0 - static sun
  // speed 1 - 60: sunrise time in minutes
  // speed 60 - 120 : sunset time in minutes - 60;
  // speed above: "breathing" rise and set
  if (seg.call == 0 || seg.speed != seg.aux0) {
    seg.step = seg.now;  // save starting time
    seg.aux0 = seg.speed;
  }

  seg.fill(BLACK);
  unsigned stage = 0xFFFF;

  uint32_t s10SinceStart = (seg.now - seg.step) / 100;  // tenths of seconds

  if (seg.speed > 120) {  // quick sunrise and sunset
    unsigned counter = (seg.now >> 1) * (((seg.speed - 120) >> 1) + 1);
    stage = triwave16(counter);
  } else if (seg.speed) {  // sunrise
    unsigned durMins = seg.speed;
    if (durMins > 60)
      durMins -= 60;
    uint32_t s10Target = durMins * 600;
    if (s10SinceStart > s10Target)
      s10SinceStart = s10Target;
    stage = wf_map(s10SinceStart, 0, s10Target, 0, 0xFFFF);
    if (seg.speed > 60)
      stage = 0xFFFF - stage;  // sunset
  }

  for (unsigned i = 0; i <= seg_len / 2; i++) {
    // default palette is Fire
    unsigned wave = triwave16((i * stage) / seg_len);
    wave = (wave >> 8) + ((wave * seg.intensity) >> 15);
    uint32_t c;
    if (wave > 240) {  // clipped, full white sun
      c = seg.color_from_palette(240, false, true, 255);
    } else {  // transition
      c = seg.color_from_palette(wave, false, true, 255);
    }
    seg.set_pixel_color(i, c);
    seg.set_pixel_color(seg_len - i - 1, c);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLEUP
void mode_twinkleup(Segment &seg) {  // A very short twinkle routine with fade-in and dual controls. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  Prng &prng = fx_prng();
  unsigned prevSeed = prng.get_seed();  // save seed so we can restore it at the end of the function
  prng.set_seed(535);  // The randomizer needs to be re-set each time through the loop in order for the same 'random'
                       // numbers to be the same each time through.

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned ranstart = prng.random8();  // The starting value (aka brightness) for each pixel. Must be consistent each
                                         // time through the loop for this to work.
    unsigned pixBri = sin8_t(ranstart + 16 * seg.now / (256 - seg.speed));
    if (prng.random8() > seg.intensity)
      pixBri = 0;
    seg.set_pixel_color(
        i, color_blend(seg.color(1),
                       seg.color_from_palette(prng.random8() + seg.now / 100, false, seg.palette_solid_wrap(), 0),
                       pixBri));
  }

  prng.set_seed(prevSeed);  // restore original seed so other effects can use "random" PRNG
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINE
//
// Sine waves that have controllable phase change speed, frequency and cutoff. By Andrew Tuline.
// seg.speed ->Speed, seg.intensity -> Frequency (seg.fft1 -> Color change, seg.fft2 -> PWM cutoff)
//
void mode_sinewave(Segment &seg) {  // Adjustable sinewave. By Andrew Tuline
  const unsigned seg_len = seg.length();
  // #define qsuba(x, b)  ((x>b)?x-b:0)               // Analog Unsigned subtraction macro. if result <0, then => 0

  unsigned colorIndex = seg.now / 32;  //(256 - seg.fft1);  // Amount of colour change.

  seg.step += seg.speed / 16;            // Speed of animation.
  unsigned freq = seg.intensity / 4;     // seg.fft2/8;                       // Frequency of the signal.

  for (unsigned i = 0; i < seg_len; i++) {  // For each of the LED's in the strand, set a brightness based on a wave as
                                            // follows:
    uint8_t pixBri = cubicwave8((i * freq) + seg.step);  // qsuba(cubicwave8((i*freq)+seg.step),
                                                         // (255-seg.intensity)); // qsub sets a minimum value called
                                                         // thiscutoff. If < thiscutoff, then bright = 0. Otherwise,
                                                         // bright = 128 (as defined in qsub)..
    // setPixCol(i, i*colorIndex/255, pixBri);
    seg.set_pixel_color(
        i, color_blend(seg.color(1), seg.color_from_palette(i * colorIndex / 255, false, seg.palette_solid_wrap(), 0),
                       pixBri));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHUNCHUN
/*
 * Dots waving around in a sine/pendulum motion.
 * Little pixel birds flying in a circle. By Aircoookie
 */
void mode_chunchun(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  seg.fade_out(254);  // add a bit of trail
  unsigned counter = seg.now * (6 + (seg.speed >> 4));
  unsigned numBirds = 2 + (seg_len >> 3);  // 2 + 1/8 of a segment
  unsigned span = (seg.intensity << 8) / numBirds;

  for (unsigned i = 0; i < numBirds; i++) {
    counter -= span;
    unsigned megumin = sin16_t(counter) + 0x8000;
    unsigned bird = uint32_t(megumin * seg_len) >> 16;
    bird = constrain(bird, 0U, seg_len - 1U);
    seg.set_pixel_color(bird, seg.color_from_palette((i * 255) / numBirds, false, false, 0));  // no palette wrapping
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PERLIN_MOVE
/////////////////////////
//     Perlin Move     //
/////////////////////////
// 16 bit perlinmove. Use Perlin Noise instead of sinewaves for movement. By Andrew Tuline.
// Controls are speed, # of pixels, faderate.
void mode_perlinmove(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  seg.fade_out(255 - seg.custom1);
  for (int i = 0; i < seg.intensity / 16 + 1; i++) {
    unsigned locn = perlin16(seg.now * 128 / (260 - seg.speed) + i * 15000,
                             seg.now * 128 / (260 - seg.speed));   // Get a new pixel location from moving noise.
    unsigned pixloc = wf_map(locn, 50 * 256, 192 * 256, 0, seg_len - 1);  // Map that to the length of the strand, and
                                                                          // ensure we don't go over.
    seg.set_pixel_color(pixloc, seg.color_from_palette(pixloc % 255, false, seg.palette_solid_wrap(), 0));
  }
}  // mode_perlinmove()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVESINS
/////////////////////////
//     Waveins         //
/////////////////////////
// Uses beatsin8() + phase shifting. By: Andrew Tuline
void mode_wavesins(Segment &seg) {
  const unsigned seg_len = seg.length();

  for (unsigned i = 0; i < seg_len; i++) {
    uint8_t bri = sin8_t(seg.now / 4 + i * seg.intensity);
    uint8_t index = beatsin8_t(seg.speed, seg.custom1, seg.custom1 + seg.custom2, seg.now, 0,
                               i * (seg.custom3 << 3));  // custom3 is reduced resolution slider
    // seg.set_pixel_color(i, ColorFromPalette(seg.palette_ref(), index, bri, LINEARBLEND));
    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0, bri));
  }
}  // mode_waveins()
#endif


const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE
    {"Wipe@!,!;!,!;!", mode_color_wipe},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WIPE_RANDOM
    {"Wipe Random@!;;!", mode_color_wipe_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWEEP
    {"Sweep@!,!;!,!;!", mode_color_sweep},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWEEP_RANDOM
    {"Sweep Random@!;;!", mode_color_sweep_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE
    {"Strobe@!;!,!;!;01", mode_strobe},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STROBE_RAINBOW
    {"Strobe Rainbow@!;,!;!;01", mode_strobe_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLINK_RAINBOW
    {"Blink Rainbow@Frequency,Blink duration;!,!;!;01", mode_blink_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE
    {"Twinkle@!,!;!,!;!;;m12=0", mode_twinkle},  // pixels
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLEUP
    {"Twinkleup@!,Intensity;!,!;!;;m12=0", mode_twinkleup},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FAIRYTWINKLE
    {"Fairytwinkle@!,!;!,!;!;;m12=0", mode_fairytwinkle},  // pixels
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORTWINKLES
    {"Colortwinkles@Fade speed,Spawn speed;;!;;m12=0", mode_colortwinkle},  // pixels
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLEFOX
    {"Twinklefox@!,Twinkle rate,,,,Cool;!,!;!", mode_twinklefox},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLECAT
    {"Twinklecat@!,Twinkle rate,,,,Cool,Reverse;!,!;!", mode_twinklecat},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRADIENT
    {"Gradient@!,Spread;!,!;!;;ix=16", mode_gradient},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LOADING
    {"Loading@!,Fade;!,!;!;;ix=16", mode_loading},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GLITTER
    {"Glitter@!,!,,,,,Overlay;,,Glitter color;!;;pal=11,m12=0", mode_glitter},  // pixels
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_GLITTER
    {"Solid Glitter@,!;Bg,,Glitter color;;;m12=0", mode_solid_glitter},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DYNAMIC
    {"Dynamic@!,!,,,,Smooth;;!", mode_dynamic},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DYNAMIC_SMOOTH
    {"Dynamic Smooth@!,!;;!", mode_dynamic_smooth},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROLLING_BALLS
    {"Rolling Balls@!,# of balls,,,,Collide,Overlay,Trails;!,!,!;!;1;m12=1", mode_rolling_balls},  // bar
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TETRIX
    {"Tetrix@!,Width,,,,One color;!,!;!;;sx=0,ix=0,pal=11,m12=1", mode_tetrix},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ICU
    {"ICU@!,!,,,,,Overlay;!,!;!", mode_icu},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_OSCILLATE
    {"Oscillate", mode_oscillate},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SUNRISE
    {"Sunrise@Time [min],Width;;!;;pal=35,sx=60", mode_sunrise},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_STREAM
    {"Stream@!,Zone size;;!", mode_running_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOLID_PATTERN_TRI
    {"Solid Pattern Tri@,Size;1,2,3;;;pal=0", mode_tri_static_pattern},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CHUNCHUN
    {"Chunchun@!,Gap size;!,!;!", mode_chunchun},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINE
    {"Sine@!,Scale;;!", mode_sinewave},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PERLIN_MOVE
    {"Perlin Move@!,# of pixels,Fade rate;!,!;!", mode_perlinmove},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVESINS
    {"Wavesins@!,Brightness variation,Starting color,Range of colors,Color variation;!;!", mode_wavesins},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_B;
const EffectGroup EFFECT_GROUP_1D_B{"1d_b", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_B
