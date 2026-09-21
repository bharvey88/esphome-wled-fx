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

/* Volume reactive, non-particle. Reads seg.audio(). See BATCHES.md. */

// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_AUDIO_VOL                                                                      \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE_PEAK || WLED_FX_FX_SWIRL || WLED_FX_FX_WAVERLY ||     \
   WLED_FX_FX_GRAVCENTER || WLED_FX_FX_GRAVCENTRIC || WLED_FX_FX_GRAVIMETER || WLED_FX_FX_JUGGLES || \
   WLED_FX_FX_MATRIPIX || WLED_FX_FX_MIDNOISE || WLED_FX_FX_NOISEFIRE || WLED_FX_FX_NOISEMETER ||    \
   WLED_FX_FX_PIXELWAVE || WLED_FX_FX_PLASMOID || WLED_FX_FX_PUDDLEPEAK || WLED_FX_FX_PUDDLES ||     \
   WLED_FX_FX_PIXELS)

#if WLED_FX_GROUP_AUDIO_VOL

namespace esphome {
namespace wled_fx {
namespace {

/* The Ripple and Gravity structs and mode_gravcenter_base() are engine code now,
 * in wf_fx_shared.h. */

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE_PEAK
/////////////////////////////////
//     * Ripple Peak           //
/////////////////////////////////
void mode_ripplepeak(Segment &seg) {  // * Ripple peak. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  // This currently has no controls.
  // Upstream spells this as a #define because a case label cannot take a
  // variable; a constexpr can, and ESPHome prefers it over a macro.
  constexpr int MAXSTEPS = 16;

  unsigned maxRipples = 16;
  unsigned dataSize = sizeof(Ripple) * maxRipples;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  Ripple *ripples = reinterpret_cast<Ripple *>(seg.data);

  AudioData &audio = seg.audio();
  uint8_t samplePeak = audio.sample_peak;
  // The FFT_MajorPeak branch is unconditional here: AudioData always carries the
  // field, so upstream's ESP8266 fallback to a random colour has no equivalent.
  float FFT_MajorPeak = audio.fft_major_peak;
  uint8_t *maxVol = &audio.max_vol;
  uint8_t *binNum = &audio.bin_num;

  if (seg.call == 0) {
    seg.custom1 = *binNum;
    seg.custom2 = *maxVol * 2;
  }

  *binNum = seg.custom1;      // Select a bin.
  *maxVol = seg.custom2 / 2;  // Our volume comparator.

  seg.fade_out(240);  // Lower frame rate means less effective fading than FastLED
  seg.fade_out(240);

  for (int i = 0; i < seg.intensity / 16; i++) {  // Limit the number of ripples.
    if (samplePeak)
      ripples[i].state = 255;

    switch (ripples[i].state) {
      case 254:  // Inactive mode
        break;

      case 255:  // Initialize ripple variables.
        ripples[i].pos = hw_random16(seg_len);
        if (FFT_MajorPeak > 1)  // log10(0) is "forbidden" (throws exception)
          ripples[i].color = (int) (log10f(FFT_MajorPeak) * 128);
        else
          ripples[i].color = 0;
        ripples[i].state = 0;
        break;

      case 0:
        seg.set_pixel_color(ripples[i].pos, seg.color_from_palette(ripples[i].color, false, seg.palette_solid_wrap(), 0));
        ripples[i].state++;
        break;

      case MAXSTEPS:  // At the end of the ripples. 254 is an inactive mode.
        ripples[i].state = 254;
        break;

      default:  // Middle of the ripples.
        seg.set_pixel_color((ripples[i].pos + ripples[i].state + seg_len) % seg_len,
                            color_blend(seg.color(1),
                                        seg.color_from_palette(ripples[i].color, false, seg.palette_solid_wrap(), 0),
                                        uint8_t(2 * 255 / ripples[i].state)));
        seg.set_pixel_color((ripples[i].pos - ripples[i].state + seg_len) % seg_len,
                            color_blend(seg.color(1),
                                        seg.color_from_palette(ripples[i].color, false, seg.palette_solid_wrap(), 0),
                                        uint8_t(2 * 255 / ripples[i].state)));
        ripples[i].state++;  // Next step.
        break;
    }  // switch step
  }  // for i
}  // mode_ripplepeak()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWIRL
/////////////////////////
//    * 2D Swirl       //
/////////////////////////
// By: Mark Kriegsman https://gist.github.com/kriegsman/5adca44e14ad025e6d3b , modified by Andrew Tuline
void mode_2DSwirl(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  const uint8_t borderWidth = 2;

  seg.blur(seg.custom1);

  int i = beatsin8_t(27 * seg.speed / 255, borderWidth, cols - borderWidth, seg.now);
  int j = beatsin8_t(41 * seg.speed / 255, borderWidth, rows - borderWidth, seg.now);
  int ni = (cols - 1) - i;
  int nj = (cols - 1) - j;

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;  // ewowi: use instead of sampleAvg???
  int volumeRaw = (int16_t) audio.volume_raw;

  seg.add_pixel_color_xy(i, j,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 11 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 11, 200, 255);
  seg.add_pixel_color_xy(j, i,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 13 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 13, 200, 255);
  seg.add_pixel_color_xy(ni, nj,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 17 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 17, 200, 255);
  seg.add_pixel_color_xy(nj, ni,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 29 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 29, 200, 255);
  seg.add_pixel_color_xy(i, nj,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 37 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 37, 200, 255);
  seg.add_pixel_color_xy(ni, j,
                         ColorFromPalette(seg.palette_ref(), (seg.now / 41 + volumeSmth * 4),
                                          volumeRaw * seg.intensity / 64, LINEARBLEND));  // CHSV( ms / 41, 200, 255);
}  // mode_2DSwirl()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVERLY
/////////////////////////
//    * 2D Waverly     //
/////////////////////////
// By: Stepko, https://editor.soulmatelights.com/gallery/652-wave , modified by Andrew Tuline
void mode_2DWaverly(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  seg.fade_to_black_by(seg.speed);

  long t = seg.now / 2;
  for (int i = 0; i < cols; i++) {
    unsigned thisVal = (1 + seg.intensity / 64) * perlin8(i * 45, t, t) / 2;
    // use audio if available. Upstream guards this with `if (um_data)`, which is
    // always true once getAudioData() has run; seg.audio() is never null.
    thisVal /= 32;  // reduce intensity of perlin8()
    thisVal *= volumeSmth;
    int thisMax = wf_map(thisVal, 0, 512, 0, rows);

    for (int j = 0; j < thisMax; j++) {
      seg.add_pixel_color_xy(i, j, ColorFromPalette(seg.palette_ref(), wf_map(j, 0, thisMax, 250, 0), 255, LINEARBLEND));
      seg.add_pixel_color_xy((cols - 1) - i, (rows - 1) - j,
                             ColorFromPalette(seg.palette_ref(), wf_map(j, 0, thisMax, 250, 0), 255, LINEARBLEND));
    }
  }
  if (seg.check3)
    seg.blur(16, cols * rows < 100);
}  // mode_2DWaverly()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVCENTER
void mode_gravcenter(Segment &seg) {  // Gravcenter. By Andrew Tuline.
  mode_gravcenter_base(seg, 0);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVCENTRIC
///////////////////////
//   * GRAVCENTRIC   //
///////////////////////
void mode_gravcentric(Segment &seg) {  // Gravcentric. By Andrew Tuline.
  mode_gravcenter_base(seg, 1);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVIMETER
///////////////////////
//   * GRAVIMETER    //
///////////////////////
void mode_gravimeter(Segment &seg) {  // Gravmeter. By Andrew Tuline.
  mode_gravcenter_base(seg, 2);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JUGGLES
//////////////////////
//   * JUGGLES      //
//////////////////////
void mode_juggles(Segment &seg) {  // Juggles. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  seg.fade_out(224);  // 6.25%
  uint8_t my_sampleAgc = fmax(fmin(volumeSmth, 255.0), 0);

  for (size_t i = 0; i < seg.intensity / 32 + 1U; i++) {
    // if seg_len equals 1, we will always set color to the first and only pixel, but the effect is still good looking
    seg.set_pixel_color(beatsin16_t(seg.speed / 4 + i * 2, 0, seg_len - 1, seg.now),
                        color_blend(seg.color(1),
                                    seg.color_from_palette(seg.now / 4 + i * 2, false, seg.palette_solid_wrap(), 0),
                                    my_sampleAgc));
  }
}  // mode_juggles()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MATRIPIX
//////////////////////
//   * MATRIPIX     //
//////////////////////
void mode_matripix(Segment &seg) {  // Matripix. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  // effect can work on single pixels, we just lose the shifting effect
  unsigned dataSize = sizeof(uint32_t) * seg_len;
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  uint32_t *pixels = reinterpret_cast<uint32_t *>(seg.data);

  AudioData &audio = seg.audio();
  int volumeRaw = (int16_t) audio.volume_raw;

  if (seg.call == 0) {
    for (unsigned i = 0; i < seg_len; i++)
      pixels[i] = BLACK;  // may not be needed as resetIfRequired() clears buffer
  }

  uint8_t secondHand = seg.now_us / (256 - seg.speed) / 500 % 16;
  if (seg.aux0 != secondHand) {
    seg.aux0 = secondHand;

    int pixBri = volumeRaw * seg.intensity / 64;
    unsigned k = seg_len - 1;
    // loop will not execute if seg_len equals 1
    for (unsigned i = 0; i < k; i++) {
      pixels[i] = pixels[i + 1];  // shift left
      seg.set_pixel_color(i, pixels[i]);
    }
    pixels[k] = color_blend(seg.color(1), seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0), pixBri);
    seg.set_pixel_color(k, pixels[k]);
  }
}  // mode_matripix()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MIDNOISE
//////////////////////
//   * MIDNOISE     //
//////////////////////
void mode_midnoise(Segment &seg) {  // Midnoise. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // Changing xdist to seg.aux0 and ydist to seg.aux1.

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  seg.fade_out(seg.speed);
  seg.fade_out(seg.speed);

  float tmpSound2 = volumeSmth * (float) seg.intensity / 256.0;  // Too sensitive.
  tmpSound2 *= (float) seg.intensity / 128.0;                    // Reduce sensitivity/length.

  unsigned maxLen = mapf(tmpSound2, 0, 127, 0, seg_len / 2);
  if (maxLen > seg_len / 2)
    maxLen = seg_len / 2;

  for (unsigned i = (seg_len / 2 - maxLen); i < (seg_len / 2 + maxLen); i++) {
    uint8_t index = perlin8(i * volumeSmth + seg.aux0,
                            seg.aux1 + i * volumeSmth);  // Get a value from the noise function. I'm using both x and y axis.
    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
  }

  seg.aux0 = seg.aux0 + beatsin8_t(5, 0, 10, seg.now);
  seg.aux1 = seg.aux1 + beatsin8_t(4, 0, 10, seg.now);
}  // mode_midnoise()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEFIRE
//////////////////////
//   * NOISEFIRE    //
//////////////////////
// I am the god of hellfire. . . Volume (only) reactive fire routine. Oh, look how short this is.
void mode_noisefire(Segment &seg) {  // Noisefire. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  CRGBPalette16 myPal = CRGBPalette16(CHSV(0, 255, 20), CHSV(0, 255, 30), CHSV(0, 255, 40),
                                      CHSV(0, 255, 44),  // Fire palette definition. Lower value = darker.
                                      CHSV(0, 255, 64), CRGB::Red, CRGB::Red, CRGB::Red, CRGB::DarkOrange,
                                      CRGB::DarkOrange, CRGB::Orange, CRGB::Orange, CRGB::Yellow, CRGB::Orange,
                                      CRGB::Yellow, CRGB::Yellow);

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  if (seg.call == 0)
    seg.fill(BLACK);

  for (unsigned i = 0; i < seg_len; i++) {
    unsigned index = perlin8(i * seg.speed / 64, seg.now * seg.speed / 64 * seg_len / 255);  // X location is constant, but we move along the Y at the rate of millis(). By Andrew Tuline.
    index = (255 - i * 256 / seg_len) * index / (256 - seg.intensity);  // Now we need to scale index so that it gets blacker as we get close to one of the ends.
                                                                        // This is a simple y=mx+b equation that's been scaled. index/128 is another scaling.

    seg.set_pixel_color(i, ColorFromPalette(myPal, index, volumeSmth * 2, LINEARBLEND));  // Use my own palette.
  }
}  // mode_noisefire()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEMETER
///////////////////////
//   * Noisemeter    //
///////////////////////
void mode_noisemeter(Segment &seg) {  // Noisemeter. By Andrew Tuline.
  const unsigned seg_len = seg.length();

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;
  int volumeRaw = (int16_t) audio.volume_raw;

  // uint8_t fadeRate = wf_map(seg.speed,0,255,224,255);
  uint8_t fadeRate = wf_map(seg.speed, 0, 255, 200, 254);
  seg.fade_out(fadeRate);

  float tmpSound2 = volumeRaw * 2.0 * (float) seg.intensity / 255.0;
  unsigned maxLen = mapf(tmpSound2, 0, 255, 0, seg_len);  // map to pixels availeable in current segment              // Still a bit too sensitive.
  // Upstream also has `if (maxLen < 0) maxLen = 0;` here. maxLen is unsigned, so
  // it is dead code and it warns under -Wextra; the clamp below is the live one.
  if (maxLen > seg_len)
    maxLen = seg_len;

  for (unsigned i = 0; i < maxLen; i++) {  // The louder the sound, the wider the soundbar. By Andrew Tuline.
    uint8_t index = perlin8(i * volumeSmth + seg.aux0,
                            seg.aux1 + i * volumeSmth);  // Get a value from the noise function. I'm using both x and y axis.
    seg.set_pixel_color(i, seg.color_from_palette(index, false, seg.palette_solid_wrap(), 0));
  }

  seg.aux0 += beatsin8_t(5, 0, 10, seg.now);
  seg.aux1 += beatsin8_t(4, 0, 10, seg.now);
}  // mode_noisemeter()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PIXELWAVE
//////////////////////
//   * PIXELWAVE    //
//////////////////////
void mode_pixelwave(Segment &seg) {  // Pixelwave. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  // even with 1D effect we have to take logic for 2D segments for allocation as fill_solid() fills whole segment

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  AudioData &audio = seg.audio();
  int volumeRaw = (int16_t) audio.volume_raw;

  uint8_t secondHand = seg.now_us / (256 - seg.speed) / 500 + 1 % 16;
  if (seg.aux0 != secondHand) {
    seg.aux0 = secondHand;

    uint8_t pixBri = volumeRaw * seg.intensity / 64;

    seg.set_pixel_color(seg_len / 2,
                        color_blend(seg.color(1),
                                    seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0), pixBri));
    for (unsigned i = seg_len - 1; i > seg_len / 2; i--)
      seg.set_pixel_color(i, seg.get_pixel_color(i - 1));  // move to the left
    for (unsigned i = 0; i < seg_len / 2; i++)
      seg.set_pixel_color(i, seg.get_pixel_color(i + 1));  // move to the right
  }
}  // mode_pixelwave()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMOID
//////////////////////
//   * PLASMOID     //
//////////////////////
typedef struct Plasphase {
  int16_t thisphase;
  int16_t thatphase;
} plasphase;

void mode_plasmoid(Segment &seg) {  // Plasmoid. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  // even with 1D effect we have to take logic for 2D segments for allocation as fill_solid() fills whole segment
  if (!seg.allocate_data(sizeof(plasphase)))
    FX_FALLBACK_STATIC;  // allocation failed
  Plasphase *plasmoip = reinterpret_cast<Plasphase *>(seg.data);

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  seg.fade_to_black_by(32);

  plasmoip->thisphase += beatsin8_t(6, -4, 4, seg.now);  // You can change direction and speed individually.
  plasmoip->thatphase += beatsin8_t(7, -4, 4, seg.now);  // Two phase values to make a complex pattern. By Andrew Tuline.

  for (unsigned i = 0; i < seg_len; i++) {  // For each of the LED's in the strand, set a brightness based on a wave as follows.
    // updated, similar to "plasma" effect - softhack007
    uint8_t thisbright = cubicwave8(((i * (1 + (3 * seg.speed / 32))) + plasmoip->thisphase) & 0xFF) / 2;
    thisbright += cos8_t(((i * (97 + (5 * seg.speed / 32))) + plasmoip->thatphase) & 0xFF) / 2;  // Let's munge the brightness a bit and animate it all with the phases.

    uint8_t colorIndex = thisbright;
    if (volumeSmth * seg.intensity / 64 < thisbright) {
      thisbright = 0;
    }

    seg.add_pixel_color(i, color_blend(seg.color(1),
                                       seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0),
                                       thisbright));
  }
}  // mode_plasmoid()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PUDDLEPEAK || WLED_FX_FX_PUDDLES
//////////////////////
//   * PUDDLES      //
//////////////////////
// Puddles/Puddlepeak By Andrew Tuline. Merged by @dedehai
void mode_puddles_base(Segment &seg, bool peakdetect) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  unsigned size = 0;
  uint8_t fadeVal = wf_map(seg.speed, 0, 255, 224, 254);
  unsigned pos = hw_random16(seg_len);  // Set a random starting position.
  seg.fade_out(fadeVal);

  AudioData &audio = seg.audio();
  int volumeRaw = (int16_t) audio.volume_raw;
  uint8_t samplePeak = audio.sample_peak;
  uint8_t *maxVol = &audio.max_vol;
  uint8_t *binNum = &audio.bin_num;
  float volumeSmth = audio.volume_smth;

  if (peakdetect) {             // puddles peak
    *binNum = seg.custom1;      // Select a bin.
    *maxVol = seg.custom2 / 2;  // Our volume comparator.
    if (samplePeak == 1) {
      size = volumeSmth * seg.intensity / 256 / 4 + 1;  // Determine size of the flash based on the volume.
      if (pos + size >= seg_len)
        size = seg_len - pos;
    }
  } else {  // puddles
    if (volumeRaw > 1) {
      size = volumeRaw * seg.intensity / 256 / 8 + 1;  // Determine size of the flash based on the volume.
      if (pos + size >= seg_len)
        size = seg_len - pos;
    }
  }

  for (unsigned i = 0; i < size; i++) {  // Flash the LED's.
    seg.set_pixel_color(pos + i, seg.color_from_palette(seg.now, false, seg.palette_solid_wrap(), 0));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PUDDLEPEAK
void mode_puddlepeak(Segment &seg) {  // Puddlepeak. By Andrew Tuline.
  mode_puddles_base(seg, true);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PUDDLES
void mode_puddles(Segment &seg) {  // Puddles. By Andrew Tuline.
  mode_puddles_base(seg, false);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PIXELS
//////////////////////
//     * PIXELS     //
//////////////////////
void mode_pixels(Segment &seg) {  // Pixels. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;

  if (!seg.allocate_data(32 * sizeof(uint8_t)))
    FX_FALLBACK_STATIC;  // allocation failed
  uint8_t *myVals = reinterpret_cast<uint8_t *>(seg.data);  // Used to store a pile of samples because WLED frame rate and WLED sample rate are not synchronized. Frame rate is too low.

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  myVals[seg.now % 32] = volumeSmth;  // filling values semi randomly

  seg.fade_out(64 + (seg.speed >> 1));

  for (int i = 0; i < seg.intensity / 8; i++) {
    unsigned segLoc = hw_random16(seg_len);  // 16 bit for larger strands of LED's.
    seg.set_pixel_color(segLoc, color_blend(seg.color(1),
                                            seg.color_from_palette(myVals[i % 32] + i * 4, false,
                                                                   seg.palette_solid_wrap(), 0),
                                            uint8_t(volumeSmth)));
  }
}  // mode_pixels()
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE_PEAK
    {"Ripple Peak@Fade rate,Max # of ripples,Select bin,Volume (min);!,!;!;1v;c2=0,m12=0,si=0", mode_ripplepeak},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SWIRL
    {"Swirl@!,Sensitivity,Blur;,Bg Swirl;!;2v;ix=64,si=0", mode_2DSwirl},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVERLY
    {"Waverly@Amplification,Sensitivity,,,,,Blur;;!;2v;ix=64,si=0", mode_2DWaverly},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVCENTER
    {"Gravcenter@Rate of fall,Sensitivity;!,!;!;1v;ix=128,m12=2,si=0", mode_gravcenter},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVCENTRIC
    {"Gravcentric@Rate of fall,Sensitivity;!,!;!;1v;ix=128,m12=3,si=0", mode_gravcentric},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVIMETER
    {"Gravimeter@Rate of fall,Sensitivity;!,!;!;1v;ix=128,m12=2,si=0", mode_gravimeter},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JUGGLES
    {"Juggles@!,# of balls;!,!;!;01v;m12=0,si=0", mode_juggles},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MATRIPIX
    {"Matripix@!,Brightness;!,!;!;1v;ix=64,m12=2,si=1", mode_matripix},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MIDNOISE
    {"Midnoise@Fade rate,Max. length;!,!;!;1v;ix=128,m12=1,si=0", mode_midnoise},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEFIRE
    {"Noisefire@!,!;;;01v;m12=2,si=0", mode_noisefire},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEMETER
    {"Noisemeter@Fade rate,Width;!,!;!;1v;ix=128,m12=2,si=0", mode_noisemeter},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PIXELWAVE
    {"Pixelwave@!,Sensitivity;!,!;!;1v;ix=64,m12=2,si=0", mode_pixelwave},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMOID
    {"Plasmoid@Phase,# of pixels;!,!;!;01v;sx=128,ix=128,m12=0,si=0", mode_plasmoid},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PUDDLEPEAK
    {"Puddlepeak@Fade rate,Puddle size,Select bin,Volume (min);!,!;!;1v;c2=0,m12=0,si=0", mode_puddlepeak},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PUDDLES
    {"Puddles@Fade rate,Puddle size;!,!;!;1v;m12=0,si=0", mode_puddles},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PIXELS
    {"Pixels@Fade rate,# of pixels;!,!;!;1v;m12=0,si=0", mode_pixels},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_AUDIO_VOL;
const EffectGroup EFFECT_GROUP_AUDIO_VOL{"audio_vol", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_AUDIO_VOL
