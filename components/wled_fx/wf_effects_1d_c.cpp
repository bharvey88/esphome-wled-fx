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

/* 1D batch C. See BATCHES.md for the effect list this file owns. */

#include <algorithm>

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_1D_C                                                                             \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE || WLED_FX_FX_DISSOLVE_RND || WLED_FX_FX_SCAN ||      \
   WLED_FX_FX_SCAN_DUAL || WLED_FX_FX_SCANNER || WLED_FX_FX_SCANNER_DUAL || WLED_FX_FX_SINELON ||      \
   WLED_FX_FX_SINELON_DUAL || WLED_FX_FX_SINELON_RAINBOW || WLED_FX_FX_SPOTS ||                        \
   WLED_FX_FX_SPOTS_FADE || WLED_FX_FX_WASHING_MACHINE || WLED_FX_FX_PACMAN ||                         \
   WLED_FX_FX_TV_SIMULATOR || WLED_FX_FX_DRIP || WLED_FX_FX_FAIRY || WLED_FX_FX_METEOR ||              \
   WLED_FX_FX_TRI_FADE || WLED_FX_FX_TRAFFIC_LIGHT || WLED_FX_FX_FIRE_FLICKER || WLED_FX_FX_TWO_DOTS || \
   WLED_FX_FX_JUGGLE || WLED_FX_FX_COLORLOOP || WLED_FX_FX_FADE)

#if WLED_FX_GROUP_1D_C

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WASHING_MACHINE
/*
 * Generates a tristate square wave w/ attac & decay
 * @param x input value 0-255
 * @param pulsewidth 0-127
 * @param attdec attack & decay, max. pulsewidth / 2
 * @returns signed waveform value
 */
int8_t tristate_square8(uint8_t x, uint8_t pulsewidth, uint8_t attdec) {
  int8_t a = 127;
  if (x > 127) {
    a = -127;
    x -= 127;
  }

  if (x < attdec) {  // inc to max
    return (int16_t) x * a / attdec;
  } else if (x < pulsewidth - attdec) {  // max
    return a;
  } else if (x < pulsewidth) {  // dec to 0
    return (int16_t) (pulsewidth - x) * a / attdec;
  }
  return 0;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FADE
/*
 * Fades the LEDs between two colors
 */
void mode_fade(Segment &seg) {
  const unsigned seg_len = seg.length();
  unsigned counter = (seg.now * ((seg.speed >> 3) + 10));
  uint8_t lum = triwave16(counter) >> 8;

  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, color_blend(seg.color(1), seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0), lum));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCAN || WLED_FX_FX_SCAN_DUAL
/*
 * Scan mode parent function
 */
void scan(Segment &seg, bool dual) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  uint32_t cycleTime = 750 + (255 - seg.speed) * 150;
  uint32_t perc = seg.now % cycleTime;
  int prog = (perc * 65535) / cycleTime;
  int size = 1 + ((seg.intensity * seg_len) >> 9);
  int ledIndex = (prog * ((seg_len * 2) - size * 2)) >> 16;

  if (!seg.check2)
    seg.fill(seg.color(1));

  int led_offset = ledIndex - (seg_len - size);
  led_offset = abs(led_offset);

  if (dual) {
    for (int j = led_offset; j < led_offset + size; j++) {
      unsigned i2 = seg_len - 1 - j;
      seg.set_pixel_color(i2, seg.color_from_palette(i2, true, seg.palette_solid_wrap(), (seg.color(2)) ? 2 : 0));
    }
  }

  for (int j = led_offset; j < led_offset + size; j++) {
    seg.set_pixel_color(j, seg.color_from_palette(j, true, seg.palette_solid_wrap(), 0));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCAN
/*
 * Runs a single pixel back and forth.
 */
void mode_scan(Segment &seg) { scan(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCAN_DUAL
/*
 * Runs two pixel back and forth in opposite directions.
 */
void mode_dual_scan(Segment &seg) { scan(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORLOOP
/*
 * Cycles all LEDs at once through a rainbow.
 */
void mode_rainbow(Segment &seg) {
  unsigned counter = (seg.now * ((seg.speed >> 2) + 2)) & 0xFFFF;
  counter = counter >> 8;

  if (seg.intensity < 128) {
    seg.fill(color_blend(seg.color_wheel(counter), WHITE, uint8_t(128 - seg.intensity)));
  } else {
    seg.fill(seg.color_wheel(counter));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE || WLED_FX_FX_DISSOLVE_RND
/*
 * Dissolve function
 */
void dissolve(Segment &seg, uint32_t color) {
  const unsigned seg_len = seg.length();
  unsigned dataSize = sizeof(uint32_t) * seg_len;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  uint32_t *pixels = reinterpret_cast<uint32_t *>(seg.data);

  if (seg.call == 0) {
    for (unsigned i = 0; i < seg_len; i++)
      pixels[i] = seg.color(1);
    seg.aux0 = 1;
  }

  for (unsigned j = 0; j <= seg_len / 15; j++) {
    if (hw_random8() <= seg.intensity) {
      for (size_t times = 0; times < 10; times++) {  // attempt to spawn a new pixel 10 times
        unsigned i = hw_random16(seg_len);
        if (seg.aux0) {  // dissolve to primary/palette
          if (pixels[i] == seg.color(1)) {
            uint32_t c = color == seg.color(0) ? seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0) : color;
            if (seg.check2 && c == seg.color(1))
              c ^= 0x00000001;  // change the color slightly so the effect doesn't get stuck in Complete mode if color
                                // is same as bkg color
            pixels[i] = c;
            break;  // only spawn 1 new pixel per frame
          }
        } else {  // dissolve to secondary
          if (pixels[i] != seg.color(1)) {
            pixels[i] = seg.color(1);
            break;
          }
        }
      }
    }
  }
  unsigned incompletePixels = 0;
  for (unsigned i = 0; i < seg_len; i++) {
    seg.set_pixel_color(i, pixels[i]);  // fix for #4401
    if (seg.check2) {
      if (seg.aux0) {
        if (pixels[i] == seg.color(1))
          incompletePixels++;
      } else {
        if (pixels[i] != seg.color(1))
          incompletePixels++;
      }
    }
  }

  if (seg.step > (255 - seg.speed) + 15U) {
    seg.aux0 = !seg.aux0;
    seg.step = 0;
  } else {
    if (seg.check2) {
      if (incompletePixels == 0)
        seg.step++;  // only advance step once all pixels have changed
    } else
      seg.step++;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE
/*
 * Blink several LEDs on and then off
 */
void mode_dissolve(Segment &seg) { dissolve(seg, seg.check1 ? seg.color_wheel(hw_random8()) : seg.color(0)); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE_RND
/*
 * Blink several LEDs on and then off in random colors
 */
void mode_dissolve_random(Segment &seg) { dissolve(seg, seg.color_wheel(hw_random8())); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRAFFIC_LIGHT
/*
 * Emulates a traffic light.
 */
void mode_traffic_light(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  for (unsigned i = 0; i < seg_len; i++)
    seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 1));
  uint32_t mdelay = 500;
  for (unsigned i = 0; i < seg_len - 2; i += 3) {
    switch (seg.aux0) {
      case 0:
        seg.set_pixel_color(i, 0x00FF0000);
        mdelay = 150 + (100 * (uint32_t) (255 - seg.speed));
        break;
      case 1:
        seg.set_pixel_color(i, 0x00FF0000);
        mdelay = 150 + (20 * (uint32_t) (255 - seg.speed));
        seg.set_pixel_color(i + 1, 0x00EECC00);
        break;
      case 2:
        seg.set_pixel_color(i + 2, 0x0000FF00);
        mdelay = 150 + (100 * (uint32_t) (255 - seg.speed));
        break;
      case 3:
        seg.set_pixel_color(i + 1, gamma32inv(0x00EECC00));
        mdelay = 150 + (20 * (uint32_t) (255 - seg.speed));
        break;  // gamma inversion to restore original pre 16.0 looks
    }
  }

  if (seg.now - seg.step > mdelay) {
    seg.aux0++;
    if (seg.aux0 == 1 && seg.intensity > 140)
      seg.aux0 = 2;  // skip Red + Amber, to get US-style sequence
    if (seg.aux0 > 3)
      seg.aux0 = 0;
    seg.step = seg.now;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCANNER || WLED_FX_FX_SCANNER_DUAL
/*
 * K.I.T.T.
 */
void mode_larson_scanner(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;

  const unsigned speed = FRAMETIME * wf_map(seg.speed, 0, 255, 96, 2);  // map into useful range
  const unsigned pixels = seg_len / speed;                             // how many pixels to advance per frame

  seg.fade_out(255 - seg.intensity);

  if (seg.step > seg.now)
    return;  // we have a pause

  unsigned index = seg.aux1 + pixels;
  // are we slow enough to use frames per pixel?
  if (pixels == 0) {
    const unsigned frames = speed / seg_len;  // how many frames per 1 pixel
    if (seg.step++ < frames)
      return;
    seg.step = 0;
    index++;
  }

  if (index > seg_len) {
    seg.aux0 = !seg.aux0;  // change direction
    seg.aux1 = 0;          // reset position
    // set delay
    if (seg.aux0 || seg.check2)
      seg.step = seg.now + seg.custom1 * 25;  // multiply by 25ms
    else
      seg.step = 0;

  } else {
    // paint as many pixels as needed
    for (unsigned i = seg.aux1; i < index; i++) {
      unsigned j = (seg.aux0) ? i : seg_len - 1 - i;
      uint32_t c = seg.color_from_palette(j, true, seg.palette_solid_wrap(), 0);
      seg.set_pixel_color(j, c);
      if (seg.check1) {
        seg.set_pixel_color(seg_len - 1 - j, seg.color(2) ? seg.color(2) : c);
      }
    }
    seg.aux1 = index;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCANNER_DUAL
/*
 * Creates two Larson scanners moving in opposite directions
 * Custom mode by Keith Lord: https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/DualLarson.h
 */
void mode_dual_larson_scanner(Segment &seg) {
  seg.check1 = true;
  mode_larson_scanner(seg);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRE_FLICKER
/*
 * Fire flicker function
 */
void mode_fire_flicker(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint32_t cycleTime = 40 + (255 - seg.speed);
  uint32_t it = seg.now / cycleTime;
  if (seg.step == it)
    return;

  uint8_t w = (seg.color(0) >> 24);
  uint8_t r = (seg.color(0) >> 16);
  uint8_t g = (seg.color(0) >> 8);
  uint8_t b = (seg.color(0));
  uint8_t lum = (seg.palette == 0) ? std::max(w, std::max(r, std::max(g, b))) : 255;
  lum /= (((256 - seg.intensity) / 16) + 1);
  for (unsigned i = 0; i < seg_len; i++) {
    uint8_t flicker = hw_random8(lum);
    if (seg.palette == 0) {
      seg.set_pixel_color(i, std::max(r - flicker, 0), std::max(g - flicker, 0), std::max(b - flicker, 0),
                          std::max(w - flicker, 0));
    } else {
      seg.set_pixel_color(i, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 0, 255 - flicker));
    }
  }

  seg.step = it;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWO_DOTS
/*
 * Two dots running
 */
void mode_two_dots(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned delay = 1 + (FRAMETIME << 3) / seg_len;  // longer segments should change faster
  uint32_t it = seg.now / wf_map(seg.speed, 0, 255, delay << 4, delay);
  unsigned offset = it % seg_len;
  unsigned width = ((seg_len * (seg.intensity + 1)) >> 9);  // max width is half the strip
  if (!width)
    width = 1;
  if (!seg.check2)
    seg.fill(seg.color(2));
  const uint32_t color1 = seg.color(0);
  const uint32_t color2 = (seg.color(1) == seg.color(2)) ? color1 : seg.color(1);
  for (unsigned i = 0; i < width; i++) {
    unsigned indexR = (offset + i) % seg_len;
    unsigned indexB = (offset + i + (seg_len >> 1)) % seg_len;
    seg.set_pixel_color(indexR, color1);
    seg.set_pixel_color(indexB, color2);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FAIRY
/*
 * Fairy, inspired by https://www.youtube.com/watch?v=zeOw5MZWq24
 */
// 4 bytes
typedef struct Flasher {
  uint16_t stateStart;
  uint8_t stateDur;
  bool stateOn;
} flasher;

constexpr unsigned FLASHERS_PER_ZONE = 6;
constexpr unsigned MAX_SHIMMER = 92;

void mode_fairy(Segment &seg) {
  const unsigned seg_len = seg.length();
  // set every pixel to a 'random' color from palette (using seed so it doesn't change between frames)
  uint16_t PRNG16 = 5100 + 0;
  for (unsigned i = 0; i < seg_len; i++) {
    PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;  // next 'random' number
    seg.set_pixel_color(i, seg.color_from_palette(PRNG16 >> 8, false, false, 0));
  }

  // amount of flasher pixels depending on intensity (0: none, 255: every LED)
  if (seg.intensity == 0)
    return;
  unsigned flasherDistance = ((255 - seg.intensity) / 28) + 1;  // 1-10
  unsigned numFlashers = (seg_len / flasherDistance) + 1;

  unsigned dataSize = sizeof(flasher) * numFlashers;
  if (!seg.allocate_data(dataSize))
    return;  // allocation failed
  Flasher *flashers = reinterpret_cast<Flasher *>(seg.data);
  unsigned now16 = seg.now & 0xFFFF;

  // Up to 11 flashers in one brightness zone, afterwards a new zone for every 6 flashers
  unsigned zones = numFlashers / FLASHERS_PER_ZONE;
  if (!zones)
    zones = 1;
  unsigned flashersInZone = numFlashers / zones;
  uint8_t flasherBri[FLASHERS_PER_ZONE * 2 - 1];

  for (unsigned z = 0; z < zones; z++) {
    unsigned flasherBriSum = 0;
    unsigned firstFlasher = z * flashersInZone;
    if (z == zones - 1)
      flashersInZone = numFlashers - (flashersInZone * (zones - 1));

    for (unsigned f = firstFlasher; f < firstFlasher + flashersInZone; f++) {
      unsigned stateTime = uint16_t(now16 - flashers[f].stateStart);
      // random on/off time reached, switch state
      if (stateTime > flashers[f].stateDur * 10) {
        flashers[f].stateOn = !flashers[f].stateOn;
        if (flashers[f].stateOn) {
          flashers[f].stateDur = 12 + hw_random8(12 + ((255 - seg.speed) >> 2));  //*10, 250ms to 1250ms
        } else {
          flashers[f].stateDur = 20 + hw_random8(6 + ((255 - seg.speed) >> 2));  //*10, 250ms to 1250ms
        }
        // flashers[f].stateDur = 51 + hw_random8(2 + ((255 - seg.speed) >> 1));
        flashers[f].stateStart = now16;
        if (stateTime < 255) {
          flashers[f].stateStart -= 255 - stateTime;  // start early to get correct bri
          flashers[f].stateDur += 26 - stateTime / 10;
          stateTime = 255 - stateTime;
        } else {
          stateTime = 0;
        }
      }
      if (stateTime > 255)
        stateTime = 255;  // for flasher brightness calculation, fades in first 255 ms of state
      // flasherBri[f - firstFlasher] = (flashers[f].stateOn) ? 255-seg.gamma8((510 - stateTime) >> 1) :
      // seg.gamma8((510 - stateTime) >> 1);
      flasherBri[f - firstFlasher] = (flashers[f].stateOn) ? stateTime : 255 - (stateTime >> 0);
      flasherBriSum += flasherBri[f - firstFlasher];
    }
    // dim factor, to create "shimmer" as other pixels get less voltage if a lot of flashers are on
    unsigned avgFlasherBri = flasherBriSum / flashersInZone;
    unsigned globalPeakBri = 255 - ((avgFlasherBri * MAX_SHIMMER) >> 8);  // 183-255, suitable for 1/5th of LEDs flashers

    for (unsigned f = firstFlasher; f < firstFlasher + flashersInZone; f++) {
      uint8_t bri = (flasherBri[f - firstFlasher] * globalPeakBri) / 255;
      PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;  // next 'random' number
      unsigned flasherPos = f * flasherDistance;
      seg.set_pixel_color(flasherPos,
                          color_blend(seg.color(1), seg.color_from_palette(PRNG16 >> 8, false, false, 0), bri));
      for (unsigned i = flasherPos + 1; i < flasherPos + flasherDistance && i < seg_len; i++) {
        PRNG16 = (uint16_t) (PRNG16 * 2053) + 1384;  // next 'random' number
        seg.set_pixel_color(i, seg.color_from_palette(PRNG16 >> 8, false, false, 0, globalPeakBri));
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRI_FADE
/*
 * Fades between 3 colors
 * Custom mode by Keith Lord: https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/TriFade.h
 * Modified by Aircoookie
 */
void mode_tricolor_fade(Segment &seg) {
  const unsigned seg_len = seg.length();
  uint16_t counter = seg.now * ((seg.speed >> 3) + 1);
  uint32_t prog = (counter * 768) >> 16;

  uint32_t color1 = 0, color2 = 0;
  unsigned stage = 0;

  if (prog < 256) {
    color1 = seg.color(0);
    color2 = seg.color(1);
    stage = 0;
  } else if (prog < 512) {
    color1 = seg.color(1);
    color2 = seg.color(2);
    stage = 1;
  } else {
    color1 = seg.color(2);
    color2 = seg.color(0);
    stage = 2;
  }

  uint8_t stp = prog;  // % 256
  for (unsigned i = 0; i < seg_len; i++) {
    uint32_t color;
    if (stage == 2) {
      color = color_blend(seg.color_from_palette(i, true, seg.palette_solid_wrap(), 2), color2, stp);
    } else if (stage == 1) {
      color = color_blend(color1, seg.color_from_palette(i, true, seg.palette_solid_wrap(), 2), stp);
    } else {
      color = color_blend(color1, color2, stp);
    }
    seg.set_pixel_color(i, color);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JUGGLE
// eight colored dots, weaving in and out of sync with each other
void mode_juggle(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;

  seg.fade_to_black_by(192 - (3 * seg.intensity / 4));
  CRGB fastled_col;
  uint8_t dothue = 0;
  for (int i = 0; i < 8; i++) {
    int index = 0 + beatsin88_t((16 + seg.speed) * (i + 7), 0, seg_len - 1, seg.now);
    fastled_col = CRGB(seg.get_pixel_color(index));
    fastled_col |= (seg.palette == 0) ? CHSV(dothue, 220, 255) : CRGB(ColorFromPalette(seg.palette_ref(), dothue, 255));
    seg.set_pixel_color(index, fastled_col);
    dothue += 32;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METEOR
// meteor effect & meteor smooth (merged by @dedehai)
// send a meteor from begining to to the end of the strip with a trail that randomly decays.
// adapted from https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/#LEDStripEffectMeteorRain
void mode_meteor(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  if (!seg.allocate_data(seg_len))
    FX_FALLBACK_STATIC;  // allocation failed
  const bool meteorSmooth = seg.check3;
  uint8_t *trail = seg.data;

  const unsigned meteorSize = 1 + seg_len / 20;  // 5%
  uint16_t meteorstart;
  if (meteorSmooth)
    meteorstart = wf_map((seg.step >> 6 & 0xFF), 0, 255, 0, seg_len - 1);
  else {
    unsigned counter = seg.now * ((seg.speed >> 2) + 8);
    meteorstart = (counter * seg_len) >> 16;
  }

  const int max = seg.palette == 5 || !seg.check1 ? 240 : 255;
  // fade all leds to colors[1] in LEDs one step
  for (unsigned i = 0; i < seg_len; i++) {
    uint32_t col;
    if (hw_random8() <= 255 - seg.intensity) {
      if (meteorSmooth) {
        if (trail[i] > 0) {
          int change = trail[i] + 4 - hw_random8(24);  // change each time between -20 and +4
          trail[i] = constrain(change, 0, max);
        }
        col = seg.check1 ? seg.color_from_palette(i, true, false, 0, trail[i])
                         : seg.color_from_palette(trail[i], false, true, 255);
      } else {
        trail[i] = scale8(trail[i], 128 + hw_random8(127));
        int index = trail[i];
        int idx = 255;
        int bri = seg.palette == 35 || seg.palette == 36 ? 255 : trail[i];
        if (!seg.check1) {
          idx = 0;
          index = wf_map(i, 0, seg_len, 0, max);
          bri = trail[i];
        }
        col = seg.color_from_palette(index, false, false, idx, bri);  // full brightness for Fire
      }
      seg.set_pixel_color(i, col);
    }
  }

  // draw meteor
  for (unsigned j = 0; j < meteorSize; j++) {
    unsigned index = (meteorstart + j) % seg_len;
    if (meteorSmooth) {
      trail[index] = max;
      uint32_t col = seg.check1 ? seg.color_from_palette(index, true, false, 0, trail[index])
                                : seg.color_from_palette(trail[index], false, true, 255);
      seg.set_pixel_color(index, col);
    } else {
      int idx = 255;
      int i = trail[index] = max;
      if (!seg.check1) {
        i = wf_map(index, 0, seg_len, 0, max);
        idx = 0;
      }
      uint32_t col = seg.color_from_palette(i, false, false, idx, 255);  // full brightness
      seg.set_pixel_color(index, col);
    }
  }

  seg.step += seg.speed + 1;
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPOTS || WLED_FX_FX_SPOTS_FADE
void spots_base(Segment &seg, uint16_t threshold) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  if (!seg.check2)
    seg.fill(seg.color(1));

  unsigned maxZones = seg_len >> 2;
  unsigned zones = 1 + ((seg.intensity * maxZones) >> 8);
  unsigned zoneLen = seg_len / zones;
  unsigned offset = (seg_len - zones * zoneLen) >> 1;

  for (unsigned z = 0; z < zones; z++) {
    unsigned pos = offset + z * zoneLen;
    for (unsigned i = 0; i < zoneLen; i++) {
      unsigned wave = triwave16((i * 0xFFFF) / zoneLen);
      if (wave > threshold) {
        unsigned index = 0 + pos + i;
        unsigned s = (wave - threshold) * 255 / (0xFFFF - threshold);
        seg.set_pixel_color(index, color_blend(seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0),
                                               seg.color(1), uint8_t(255 - s)));
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPOTS
// Intensity slider sets number of "lights", speed sets LEDs per light
void mode_spots(Segment &seg) { spots_base(seg, (255 - seg.speed) << 8); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPOTS_FADE
// Intensity slider sets number of "lights", LEDs per light fade in and out
void mode_spots_fade(Segment &seg) {
  unsigned counter = seg.now * ((seg.speed >> 2) + 8);
  unsigned t = triwave16(counter);
  unsigned tr = (t >> 1) + (t >> 2);
  spots_base(seg, tr);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PACMAN
/*
/  Pac-Man by Bob Loeffler with help from @dedehai and @blazoncek
*   speed slider is for speed.
*   intensity slider is for selecting the number of power dots.
*   custom1 slider is for selecting the LED where the ghosts will start blinking blue.
*   custom2 slider is for blurring the LEDs in the segment.
*   custom3 slider is for selecting the # of ghosts (between 2 and 8).
*   check1 is for displaying White Dots that PacMan eats.  Enabled will show white dots.  Disabled will not show any
white dots (all leds will be black).
*   check2 is for Smear mode (enabled will smear/persist the LED colors, disabled will not).
*   check3 is for the Compact Dots mode of displaying white dots.  Enabled will show white dots in every LED.  Disabled
will show black LEDs between the white dots.
*   aux0 is used to keep track of the previous number of power dots in case the user selects a different number with the
intensity slider.
*   aux1 is the main counter for timing.
*/
typedef struct PacManChars {
  signed pos;
  signed topPos;      // LED position of farthest PacMan has moved
  uint32_t color;
  bool direction;  // true = moving away from first LED
  bool blue;       // used for ghosts only
  bool eaten;      // used for power dots only
} pacmancharacters_t;

void mode_pacman(Segment &seg) {
  const unsigned seg_len = seg.length();
  constexpr unsigned ORANGEYELLOW = 0xFFCC00;
  constexpr unsigned PURPLEISH = 0xB000B0;
  constexpr unsigned ORANGEISH = 0xFF8800;
  constexpr unsigned WHITEISH = 0x999999;
  constexpr unsigned PACMAN = 0;  // PacMan is character[0]
  constexpr uint32_t ghostColors[] = {RED, PURPLEISH, CYAN, ORANGEISH};

  unsigned maxPowerDots = std::min(seg_len / 10U, 255U);  // cap the max so packed state fits in 8 bits
  unsigned numPowerDots = wf_map(seg.intensity, 0, 255, 1, maxPowerDots);
  unsigned numGhosts = wf_map(seg.custom3, 0, 31, 2, 8);
  bool smearMode = seg.check2;

  // Pack two 8-bit values into one 16-bit field (stored in seg.aux0)
  uint16_t combined_value = uint16_t(((numPowerDots & 0xFF) << 8) | (numGhosts & 0xFF));
  if (combined_value != seg.aux0)
    seg.call = 0;  // Reinitialize on setting change
  seg.aux0 = combined_value;

  // Allocate segment data
  unsigned dataSize = sizeof(pacmancharacters_t) * (numGhosts + maxPowerDots + 1);  // +1 is the PacMan character
  if (seg_len <= 16 + (2 * numGhosts) || !seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;
  pacmancharacters_t *character = reinterpret_cast<pacmancharacters_t *>(seg.data);

  // Calculate when blue ghosts start blinking.
  // On first call (or after settings change), `topPos` is not known yet, so fall back to the full segment length in
  // that case.
  int maxBlinkPos = (seg.call == 0) ? (int) seg_len - 1 : character[PACMAN].topPos;
  if (maxBlinkPos < 20)
    maxBlinkPos = 20;
  int startBlinkingGhostsLED = (seg_len < 64) ? (int) seg_len / 3 : wf_map(seg.custom1, 0, 255, 20, maxBlinkPos);

  // Initialize characters on first call
  if (seg.call == 0) {
    // Initialize PacMan
    character[PACMAN].color = YELLOW;
    character[PACMAN].pos = 0;
    character[PACMAN].topPos = 0;
    character[PACMAN].direction = true;
    character[PACMAN].blue = false;

    // Initialize ghosts with alternating colors
    for (unsigned i = 1; i <= numGhosts; i++) {
      character[i].color = ghostColors[(i - 1) % 4];
      character[i].pos = -2 * (int) (i + 1);
      character[i].direction = true;
      character[i].blue = false;
    }

    // Initialize power dots
    for (unsigned i = 0; i < numPowerDots; i++) {
      character[i + numGhosts + 1].color = ORANGEYELLOW;
      character[i + numGhosts + 1].eaten = false;
    }
    character[numGhosts + 1].pos = seg_len - 1;  // Last power dot at end
  }

  if (seg.now > seg.step) {
    seg.step = seg.now;
    seg.aux1++;
  }

  // Clear background if not in smear mode
  if (!smearMode)
    seg.fill(BLACK);

  // Draw white dots in front of PacMan if option selected
  if (seg.check1) {
    int step = seg.check3 ? 1 : 2;  // Compact or spaced dots
    for (int i = seg_len - 1; i > character[PACMAN].topPos; i -= step) {
      seg.set_pixel_color(i, WHITEISH);
    }
  }

  // Update power dot positions dynamically
  uint32_t everyXLeds = (((uint32_t) seg_len - 10U) << 8) / numPowerDots;  // Fixed-point spacing for power dots: use
                                                                          // 32-bit math to avoid overflow on long
                                                                          // segments.
  for (unsigned i = 1; i < numPowerDots; i++) {
    character[i + numGhosts + 1].pos = 10 + ((i * everyXLeds) >> 8);
  }

  // Blink power dots every 10 ticks
  if (seg.aux1 % 10 == 0) {
    uint32_t dotColor = (character[numGhosts + 1].color == ORANGEYELLOW) ? BLACK : ORANGEYELLOW;
    for (unsigned i = 0; i < numPowerDots; i++) {
      character[i + numGhosts + 1].color = dotColor;
    }
  }

  // Blink blue ghosts when nearing start
  if (seg.aux1 % 15 == 0 && character[1].blue && character[PACMAN].pos <= startBlinkingGhostsLED) {
    uint32_t ghostColor = (character[1].color == BLUE) ? WHITEISH : BLUE;
    for (unsigned i = 1; i <= numGhosts; i++) {
      character[i].color = ghostColor;
    }
  }

  // Draw uneaten power dots
  for (unsigned i = 0; i < numPowerDots; i++) {
    if (!character[i + numGhosts + 1].eaten && (unsigned) character[i + numGhosts + 1].pos < seg_len) {
      seg.set_pixel_color(character[i + numGhosts + 1].pos, character[i + numGhosts + 1].color);
    }
  }

  // Check if PacMan ate a power dot
  for (unsigned j = 0; j < numPowerDots; j++) {
    auto &dot = character[j + numGhosts + 1];
    if (character[PACMAN].pos == dot.pos && !dot.eaten) {
      // Reverse all characters - PacMan now chases ghosts
      for (unsigned i = 0; i <= numGhosts; i++) {
        character[i].direction = false;
      }
      // Turn ghosts blue
      for (unsigned i = 1; i <= numGhosts; i++) {
        character[i].color = BLUE;
        character[i].blue = true;
      }
      dot.eaten = true;
      break;  // only one power dot per frame
    }
  }

  // Reset when PacMan reaches start with blue ghosts
  if (character[1].blue && character[PACMAN].pos <= 0) {
    // Reverse direction back
    for (unsigned i = 0; i <= numGhosts; i++) {
      character[i].direction = true;
    }
    // Reset ghost colors
    for (unsigned i = 1; i <= numGhosts; i++) {
      character[i].color = ghostColors[(i - 1) % 4];
      character[i].blue = false;
    }
    // Reset power dots if last one was eaten
    if (character[numGhosts + 1].eaten) {
      for (unsigned i = 0; i < numPowerDots; i++) {
        character[i + numGhosts + 1].eaten = false;
      }
      character[PACMAN].topPos = 0;  // set the top position of PacMan to LED 0 (beginning of the segment)
    }
  }

  // Update and draw characters based on speed setting
  bool updatePositions = (seg.aux1 % wf_map(seg.speed, 0, 255, 15, 1) == 0);

  // update positions of characters if it's time to do so
  if (updatePositions) {
    character[PACMAN].pos += character[PACMAN].direction ? 1 : -1;
    for (unsigned i = 1; i <= numGhosts; i++) {
      character[i].pos += character[i].direction ? 1 : -1;
    }
  }

  // Draw PacMan
  if ((unsigned) character[PACMAN].pos < seg_len) {
    seg.set_pixel_color(character[PACMAN].pos, character[PACMAN].color);
  }

  // Draw ghosts
  for (unsigned i = 1; i <= numGhosts; i++) {
    if ((unsigned) character[i].pos < seg_len) {
      seg.set_pixel_color(character[i].pos, character[i].color);
    }
  }

  // Track farthest position of PacMan
  if (character[PACMAN].topPos < character[PACMAN].pos) {
    character[PACMAN].topPos = character[PACMAN].pos;
  }

  seg.blur(seg.custom2 >> 1);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON || WLED_FX_FX_SINELON_DUAL || WLED_FX_FX_SINELON_RAINBOW
/*
 * Sinelon stolen from FASTLED examples
 */
void sinelon_base(Segment &seg, bool dual, bool rainbow = false) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  seg.fade_out(seg.intensity);
  unsigned pos = beatsin16_t(seg.speed / 10, 0, seg_len - 1, seg.now);
  if (seg.call == 0)
    seg.aux0 = pos;
  uint32_t color1 = seg.color_from_palette(pos, true, false, 0);
  uint32_t color2 = seg.color(2);
  if (rainbow) {
    color1 = seg.color_wheel((pos & 0x07) * 32);
  }
  seg.set_pixel_color(pos, color1);
  if (dual) {
    if (!color2)
      color2 = seg.color_from_palette(pos, true, false, 0);
    if (rainbow)
      color2 = color1;  // rainbow
    seg.set_pixel_color(seg_len - 1 - pos, color2);
  }
  if (seg.aux0 != pos) {
    if (seg.aux0 < pos) {
      for (unsigned i = seg.aux0; i < pos; i++) {
        seg.set_pixel_color(i, color1);
        if (dual)
          seg.set_pixel_color(seg_len - 1 - i, color2);
      }
    } else {
      for (unsigned i = seg.aux0; i > pos; i--) {
        seg.set_pixel_color(i, color1);
        if (dual)
          seg.set_pixel_color(seg_len - 1 - i, color2);
      }
    }
    seg.aux0 = pos;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON
void mode_sinelon(Segment &seg) { sinelon_base(seg, false); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON_DUAL
void mode_sinelon_dual(Segment &seg) { sinelon_base(seg, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON_RAINBOW
void mode_sinelon_rainbow(Segment &seg) { sinelon_base(seg, false, true); }
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIP
// each needs 20 bytes
// Spark type is used for popcorn, 1D fireworks, and drip
typedef struct Spark {
  float pos, posX;
  float vel, velX;
  uint16_t col;
  uint8_t colIndex;
} spark;

/*
 * Drip Effect
 * ported of: https://www.youtube.com/watch?v=sru2fXh4r7k
 */
void mode_drip(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // allocate segment data
  unsigned strips = seg.nr_of_v_strips();
  const int maxNumDrops = 4;
  unsigned dataSize = sizeof(spark) * maxNumDrops;
  if (!seg.allocate_data(dataSize * strips))
    FX_FALLBACK_STATIC;  // allocation failed
  Spark *drops = reinterpret_cast<Spark *>(seg.data);

  if (!seg.check2)
    seg.fill(seg.color(1));

  const auto run_strip = [&](uint16_t stripNr, Spark *drops) {
    unsigned numDrops = 1 + (seg.intensity >> 6);  // 255>>6 = 3

    float gravity = -0.0005f - (seg.speed / 50000.0f);
    gravity *= ((int) seg_len - 1) > 1 ? ((int) seg_len - 1) : 1;
    int sourcedrop = 12;

    for (unsigned j = 0; j < numDrops; j++) {
      if (drops[j].colIndex == 0) {  // init
        drops[j].pos = seg_len - 1;  // start at end
        drops[j].vel = 0;            // speed
        drops[j].col = sourcedrop;   // brightness
        drops[j].colIndex = 1;       // drop state (0 init, 1 forming, 2 falling, 5 bouncing)
      }

      seg.set_pixel_color(Segment::index_to_v_strip(seg_len - 1, stripNr),
                          color_blend(BLACK, seg.color(0), uint8_t(sourcedrop)));  // water source
      if (drops[j].colIndex == 1) {
        if (drops[j].col > 255)
          drops[j].col = 255;
        seg.set_pixel_color(Segment::index_to_v_strip(uint16_t(drops[j].pos), stripNr),
                            color_blend(BLACK, seg.color(0), uint8_t(drops[j].col)));

        drops[j].col += wf_map(seg.speed, 0, 255, 1, 6);  // swelling

        if (hw_random8() < drops[j].col / 10) {  // random drop
          drops[j].colIndex = 2;                 // fall
          drops[j].col = 255;
        }
      }
      if (drops[j].colIndex > 1) {  // falling
        if (drops[j].pos > 0) {     // fall until end of segment
          drops[j].pos += drops[j].vel;
          if (drops[j].pos < 0)
            drops[j].pos = 0;
          drops[j].vel += gravity;  // gravity is negative

          for (int i = 1; i < 7 - drops[j].colIndex; i++) {  // some minor math so we don't expand bouncing droplets
            unsigned pos =
                constrain(unsigned(drops[j].pos) + i, 0, seg_len - 1);  // this is BAD, returns a pos >= SEGLEN
                                                                        // occasionally
            seg.set_pixel_color(
                Segment::index_to_v_strip(pos, stripNr),
                color_blend(BLACK, seg.color(0), uint8_t(drops[j].col / i)));  // spread pixel with fade while falling
          }

          if (drops[j].colIndex > 2) {  // during bounce, some water is on the floor
            seg.set_pixel_color(Segment::index_to_v_strip(0, stripNr),
                                color_blend(seg.color(0), BLACK, uint8_t(drops[j].col)));
          }
        } else {                        // we hit bottom
          if (drops[j].colIndex > 2) {  // already hit once, so back to forming
            drops[j].colIndex = 0;
            drops[j].col = sourcedrop;

          } else {
            if (drops[j].colIndex == 2) {     // init bounce
              drops[j].vel = -drops[j].vel / 4;  // reverse velocity with damping
              drops[j].pos += drops[j].vel;
            }
            drops[j].col = sourcedrop * 2;
            drops[j].colIndex = 5;  // bouncing
          }
        }
      }
    }
  };

  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &drops[stripNr * maxNumDrops]);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WASHING_MACHINE
/*
  Imitates a washing machine, rotating same waves forward, then pause, then backward.
  By Stefan Seegel
*/
void mode_washing_machine(Segment &seg) {
  const unsigned seg_len = seg.length();
  int speed = tristate_square8(seg.now >> 7, 90, 15);

  seg.step += (speed * 2048) / (512 - seg.speed);

  for (unsigned i = 0; i < seg_len; i++) {
    uint8_t col = sin8_t(((seg.intensity / 25 + 1) * 255 * i / seg_len) + (seg.step >> 7));
    seg.set_pixel_color(i, seg.color_from_palette(col, false, seg.palette_solid_wrap(), 3));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TV_SIMULATOR
/*
  TV Simulator
  Modified and adapted to WLED by Def3nder, based on "Fake TV Light for Engineers" by Phillip Burgess
  https://learn.adafruit.com/fake-tv-light-for-engineers/arduino-sketch
*/
// 43 bytes
typedef struct TvSim {
  uint32_t totalTime = 0;
  uint32_t fadeTime = 0;
  uint32_t startTime = 0;
  uint32_t elapsed = 0;
  uint32_t pixelNum = 0;
  uint16_t sliderValues = 0;
  uint32_t sceeneStart = 0;
  uint32_t sceeneDuration = 0;
  uint16_t sceeneColorHue = 0;
  uint8_t sceeneColorSat = 0;
  uint8_t sceeneColorBri = 0;
  uint8_t actualColorR = 0;
  uint8_t actualColorG = 0;
  uint8_t actualColorB = 0;
  uint16_t pr = 0;  // Prev R, G, B
  uint16_t pg = 0;
  uint16_t pb = 0;
} tvSim;

void mode_tv_simulator(Segment &seg) {
  const unsigned seg_len = seg.length();
  int nr, ng, nb, r, g, b, i, hue;
  uint8_t sat, bri, j;

  if (!seg.allocate_data(sizeof(tvSim)))
    FX_FALLBACK_STATIC;  // allocation failed
  TvSim *tvSimulator = reinterpret_cast<TvSim *>(seg.data);

  uint8_t colorSpeed = wf_map(seg.speed, 0, UINT8_MAX, 1, 20);
  uint8_t colorIntensity = wf_map(seg.intensity, 0, UINT8_MAX, 10, 30);

  i = seg.speed << 8 | seg.intensity;
  if (i != tvSimulator->sliderValues) {
    tvSimulator->sliderValues = i;
    seg.aux1 = 0;
  }

  // create a new sceene
  if (((seg.now - tvSimulator->sceeneStart) >= tvSimulator->sceeneDuration) || seg.aux1 == 0) {
    tvSimulator->sceeneStart = seg.now;  // remember the start of the new sceene
    tvSimulator->sceeneDuration = hw_random16(60 * 250 * colorSpeed, 60 * 750 * colorSpeed);  // duration of a "movie
                                                                                              // sceene" which has
                                                                                              // similar colors (5 to 15
                                                                                              // minutes with max speed
                                                                                              // slider)
    tvSimulator->sceeneColorHue = hw_random16(0, 768);                      // random start color-tone for the sceene
    tvSimulator->sceeneColorSat = hw_random8(100, 130 + colorIntensity);    // random start color-saturation
    tvSimulator->sceeneColorBri = hw_random8(200, 240);                     // random start color-brightness
    seg.aux1 = 1;
    seg.aux0 = 0;
  }

  // slightly change the color-tone in this sceene
  if (seg.aux0 == 0) {
    // hue change in both directions
    j = hw_random8(4 * colorIntensity);
    hue = (hw_random8() < 128)
              ? ((j < tvSimulator->sceeneColorHue) ? tvSimulator->sceeneColorHue - j
                                                   : 767 - tvSimulator->sceeneColorHue - j)  // negative
              : ((j + tvSimulator->sceeneColorHue) < 767 ? tvSimulator->sceeneColorHue + j
                                                         : tvSimulator->sceeneColorHue + j - 767);  // positive

    // saturation
    j = hw_random8(2 * colorIntensity);
    sat = (tvSimulator->sceeneColorSat - j) < 0 ? 0 : tvSimulator->sceeneColorSat - j;

    // brightness
    j = hw_random8(100);
    bri = (tvSimulator->sceeneColorBri - j) < 0 ? 0 : tvSimulator->sceeneColorBri - j;

    // calculate R,G,B from HSV
    // Source: https://blog.adafruit.com/2012/03/14/constant-brightness-hsb-to-rgb-algorithm/
    {  // just to create a local scope for  the variables
      uint8_t temp[5], n = (hue >> 8) % 3;
      uint8_t x = ((((hue & 255) * sat) >> 8) * bri) >> 8;
      uint8_t s = ((256 - sat) * bri) >> 8;
      temp[0] = temp[3] = s;
      temp[1] = temp[4] = x + s;
      temp[2] = bri - x;
      tvSimulator->actualColorR = temp[n + 2];
      tvSimulator->actualColorG = temp[n + 1];
      tvSimulator->actualColorB = temp[n];
    }
  }
  // expand to 16 bit
  nr = (uint8_t) (tvSimulator->actualColorR) * 257;  // New R/G/B
  ng = (uint8_t) (tvSimulator->actualColorG) * 257;
  nb = (uint8_t) (tvSimulator->actualColorB) * 257;

  if (seg.aux0 == 0) {  // initialize next iteration
    seg.aux0 = 1;

    // randomize total duration and fade duration for the actual color
    tvSimulator->totalTime = hw_random16(250, 2500);                  // Semi-random pixel-to-pixel time
    tvSimulator->fadeTime = hw_random16(0, tvSimulator->totalTime);   // Pixel-to-pixel transition time
    if (hw_random8(10) < 3)
      tvSimulator->fadeTime = 0;  // Force scene cut 30% of time

    tvSimulator->startTime = seg.now;
  }  // end of initialization

  // how much time is elapsed ?
  tvSimulator->elapsed = seg.now - tvSimulator->startTime;

  // fade from prev color to next color
  if (tvSimulator->elapsed < tvSimulator->fadeTime) {
    r = wf_map(tvSimulator->elapsed, 0, tvSimulator->fadeTime, tvSimulator->pr, nr);
    g = wf_map(tvSimulator->elapsed, 0, tvSimulator->fadeTime, tvSimulator->pg, ng);
    b = wf_map(tvSimulator->elapsed, 0, tvSimulator->fadeTime, tvSimulator->pb, nb);
  } else {  // Avoid divide-by-zero in map()
    r = nr;
    g = ng;
    b = nb;
  }

  // set strip color
  for (i = 0; i < (int) seg_len; i++) {
    seg.set_pixel_color(i, r >> 8, g >> 8, b >> 8);  // Quantize to 8-bit
  }

  // if total duration has passed, remember last color and restart the loop
  if (tvSimulator->elapsed >= tvSimulator->totalTime) {
    tvSimulator->pr = nr;  // Prev RGB = new RGB
    tvSimulator->pg = ng;
    tvSimulator->pb = nb;
    seg.aux0 = 0;
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE
    {"Dissolve@Repeat speed,Dissolve speed,,,,Random,Complete;!,!;!", mode_dissolve},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISSOLVE_RND
    {"Dissolve Rnd@Repeat speed,Dissolve speed;,!;!", mode_dissolve_random},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCAN
    {"Scan@!,# of dots,,,,,Overlay;!,!,!;!", mode_scan},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCAN_DUAL
    {"Scan Dual@!,# of dots,,,,,Overlay;!,!,!;!", mode_dual_scan},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCANNER
    {"Scanner@!,Trail,Delay,,,Dual,Bi-delay;!,!,!;!;;m12=0,c1=0", mode_larson_scanner},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCANNER_DUAL
    {"Scanner Dual@!,Trail,Delay,,,Dual,Bi-delay;!,!,!;!;;m12=0,c1=0", mode_dual_larson_scanner},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON
    {"Sinelon@!,Trail;!,!,!;!", mode_sinelon},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON_DUAL
    {"Sinelon Dual@!,Trail;!,!,!;!", mode_sinelon_dual},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINELON_RAINBOW
    {"Sinelon Rainbow@!,Trail;,,!;!", mode_sinelon_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPOTS
    {"Spots@Spread,Width,,,,,Overlay;!,!;!", mode_spots},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPOTS_FADE
    {"Spots Fade@Spread,Width,,,,,Overlay;!,!;!", mode_spots_fade},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WASHING_MACHINE
    {"Washing Machine@!,!;;!", mode_washing_machine},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PACMAN
    {"PacMan@Speed,# of PowerDots,Blink distance,Blur,# of Ghosts,Dots,Smear,Compact;;!;1;m12=0,sx=192,ix=64,c1=64,c2="
     "0,c3=12,o1=1,o2=0",
     mode_pacman},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TV_SIMULATOR
    {"TV Simulator@!,!;;!;01", mode_tv_simulator},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIP
    {"Drip@Gravity,# of drips,,,,,Overlay;!,!;!;;m12=1", mode_drip},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FAIRY
    {"Fairy@!,# of flashers;!,!;!", mode_fairy},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METEOR
    {"Meteor@!,Trail,,,,Gradient,,Smooth;;!;1", mode_meteor},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRI_FADE
    {"Tri Fade@!;1,2,3;!", mode_tricolor_fade},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TRAFFIC_LIGHT
    {"Traffic Light@!,US style;,!;!", mode_traffic_light},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRE_FLICKER
    {"Fire Flicker@!,!;!;!;01", mode_fire_flicker},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWO_DOTS
    {"Two Dots@!,Dot size,,,,,Overlay;1,2,Bg;!", mode_two_dots},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JUGGLE
    {"Juggle@!,Trail;;!;;sx=64,ix=128", mode_juggle},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORLOOP
    {"Colorloop@!,Saturation;;!;01", mode_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FADE
    {"Fade@!;!,!;!;01", mode_fade},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D_C;
const EffectGroup EFFECT_GROUP_1D_C{"1d_c", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D_C
