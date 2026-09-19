/* Bodies of the shared WLED 16.0.1 base functions that more than one effect
 * translation unit calls. Ported from wled00/FX.cpp with the mechanical transform
 * described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * Per-effect credits are kept on the code they belong to.
 */

#include "wf_effects.h"

namespace esphome {
namespace wled_fx {

// WLED util.cpp:727
uint8_t get_random_wheel_index(uint8_t pos) {
  uint8_t r = 0, x = 0, y = 0, d = 0;
  while (d < 42) {
    r = hw_random8();
    x = abs(pos - r);
    y = 255 - x;
    d = x < y ? x : y;  // MIN(x, y)
  }
  return r;
}

// WLED FX.cpp:87, `static PRNG prng(hw_random())`. Lazily constructed so the
// hardware random number generator is not read before the platform is up.
Prng &fx_prng() {
  static Prng prng(hw_random());
  return prng;
}

// WLED FX.cpp:102
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

///////////////////////
//   * GRAVCENTER    //
///////////////////////
// Gravcenter effects By Andrew Tuline.
// Gravcenter base function for Gravcenter (0), Gravcentric (1), Gravimeter (2), Gravfreq (3) (merged by @dedehai)

void mode_gravcenter_base(Segment &seg, unsigned mode) {
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;

  const unsigned dataSize = sizeof(gravity);
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  Gravity *gravcen = reinterpret_cast<Gravity *>(seg.data);

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  if (mode == 1)
    seg.fade_out(253);  //  Gravcentric
  else if (mode == 2)
    seg.fade_out(249);  // Gravimeter
  else if (mode == 3)
    seg.fade_out(250);  // Gravfreq
  else
    seg.fade_out(251);  // Gravcenter

  float mySampleAvg;
  int tempsamp;
  float segmentSampleAvg = volumeSmth * (float) seg.intensity / 255.0f;

  if (mode == 2) {              // Gravimeter
    segmentSampleAvg *= 0.25;   // divide by 4, to compensate for later "sensitivity" upscaling
    mySampleAvg = mapf(segmentSampleAvg * 2.0, 0, 64, 0, (seg_len - 1));  // map to pixels availeable in current segment
    tempsamp = constrain(mySampleAvg, 0, seg_len - 1);  // Keep the sample from overflowing.
  } else {                       // Gravcenter or Gravcentric or Gravfreq
    segmentSampleAvg *= 0.125f;  // divide by 8, to compensate for later "sensitivity" upscaling
    mySampleAvg = mapf(segmentSampleAvg * 2.0, 0.0f, 32.0f, 0.0f,
                       (float) seg_len / 2.0f);            // map to pixels availeable in current segment
    tempsamp = constrain(mySampleAvg, 0, seg_len / 2);  // Keep the sample from overflowing.
  }

  uint8_t gravity = 8 - seg.speed / 32;
  int offset = 1;
  if (mode == 2)
    offset = 0;  // Gravimeter
  if (tempsamp >= gravcen->topLED + offset)
    gravcen->topLED = tempsamp - offset;
  else if (gravcen->gravityCounter % gravity == 0)
    gravcen->topLED--;

  if (mode == 1) {  // Gravcentric
    for (int i = 0; i < tempsamp; i++) {
      uint8_t index = segmentSampleAvg * 24 + seg.now / 200;
      seg.set_pixel_color(i + seg_len / 2, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
      seg.set_pixel_color(seg_len / 2 - 1 - i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
    }
    if (gravcen->topLED >= 0) {
      seg.set_pixel_color(gravcen->topLED + seg_len / 2, CRGB::Gray);
      seg.set_pixel_color(seg_len / 2 - 1 - gravcen->topLED, CRGB::Gray);
    }
  } else if (mode == 2) {  // Gravimeter
    for (int i = 0; i < tempsamp; i++) {
      uint8_t index = perlin8(i * segmentSampleAvg + seg.now, 5000 + i * segmentSampleAvg);
      seg.set_pixel_color(i, color_blend(seg.color(1),
                                         seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0),
                                         uint8_t(segmentSampleAvg * 8)));
    }
    if (gravcen->topLED > 0) {
      seg.set_pixel_color(gravcen->topLED, seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0));
    }
  } else if (mode == 3) {  // Gravfreq
    for (int i = 0; i < tempsamp; i++) {
      float FFT_MajorPeak = audio.fft_major_peak;  // used in mode 3: Gravfreq
      if (FFT_MajorPeak < 1)
        FFT_MajorPeak = 1;
      uint8_t index = (log10f(FFT_MajorPeak) - (MAX_FREQ_LOG10 - 1.78f)) * 255;
      seg.set_pixel_color(i + seg_len / 2, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
      seg.set_pixel_color(seg_len / 2 - i - 1, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
    }
    if (gravcen->topLED >= 0) {
      seg.set_pixel_color(gravcen->topLED + seg_len / 2, CRGB::Gray);
      seg.set_pixel_color(seg_len / 2 - 1 - gravcen->topLED, CRGB::Gray);
    }
  } else {  // Gravcenter
    for (int i = 0; i < tempsamp; i++) {
      uint8_t index = perlin8(i * segmentSampleAvg + seg.now, 5000 + i * segmentSampleAvg);
      seg.set_pixel_color(i + seg_len / 2, color_blend(seg.color(1),
                                                       seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0),
                                                       uint8_t(segmentSampleAvg * 8)));
      seg.set_pixel_color(seg_len / 2 - i - 1,
                          color_blend(seg.color(1), seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0),
                                      uint8_t(segmentSampleAvg * 8)));
    }
    if (gravcen->topLED >= 0) {
      seg.set_pixel_color(gravcen->topLED + seg_len / 2,
                          seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0));
      seg.set_pixel_color(seg_len / 2 - 1 - gravcen->topLED,
                          seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0));
    }
  }
  gravcen->gravityCounter = (gravcen->gravityCounter + 1) % gravity;
}

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

}  // namespace wled_fx
}  // namespace esphome
