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

/* FFT reactive, non-particle. Reads seg.audio(). See BATCHES.md. */

#include <algorithm>

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_AUDIO_FFT                                                                            \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVFREQ || WLED_FX_FX_FREQWAVE || WLED_FX_FX_FREQMATRIX ||        \
   WLED_FX_FX_GEQ || WLED_FX_FX_WATERFALL || WLED_FX_FX_FREQPIXELS || WLED_FX_FX_NOISEMOVE ||              \
   WLED_FX_FX_FREQMAP || WLED_FX_FX_DJ_LIGHT || WLED_FX_FX_FUNKY_PLANK || WLED_FX_FX_BLURZ ||              \
   WLED_FX_FX_ROCKTAVES || WLED_FX_FX_AKEMI)

#if WLED_FX_GROUP_AUDIO_FFT

namespace esphome {
namespace wled_fx {
namespace {

/* The Gravity struct and mode_gravcenter_base() are engine code now, in
 * wf_fx_shared.h. */

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVFREQ
///////////////////////
//    ** Gravfreq    //
///////////////////////
void mode_gravfreq(Segment &seg) {          // Gravfreq. By Andrew Tuline.
  mode_gravcenter_base(seg, 3);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQWAVE
//////////////////////
//   ** Freqwave    //
//////////////////////
// Assign a color to the central (starting pixels) based on the predominant frequencies and the volume. The color is being determined by mapping the MajorPeak from the FFT
// and then mapping this to the HSV color circle. Currently we are sampling at 10240 Hz, so the highest frequency we can look at is 5120Hz.
//
// SEGMENT.custom1: the lower cut off point for the FFT. (many, most time the lowest values have very little information since they are FFT conversion artifacts. Suggested value is close to but above 0
// SEGMENT.custom2: The high cut off point. This depends on your sound profile. Most music looks good when this slider is between 50% and 100%.
// SEGMENT.custom3: "preamp" for the audio signal for audio10.
//
// I suggest that for this effect you turn the brightness to 95%-100% but again it depends on your soundprofile you find yourself in.
// Instead of using colorpalettes, This effect works on the HSV color circle with red being the lowest frequency
//
// As a compromise between speed and accuracy we are currently sampling with 10240Hz, from which we can then determine with a 512bin FFT our max frequency is 5120Hz.
// Depending on the music stream you have you might find it useful to change the frequency mapping.
void mode_freqwave(Segment &seg) {          // Freqwave. By Andreas Pleschung.
  const unsigned seg_len = seg.length();
  // As before, this effect can also work on single pixels, we just lose the shifting effect
  AudioData &audio = seg.audio();
  float FFT_MajorPeak = audio.fft_major_peak;
  float volumeSmth    = audio.volume_smth;

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  uint8_t secondHand = seg.now_us/(256-seg.speed)/500 % 16;
  if(seg.aux0 != secondHand) {
    seg.aux0 = secondHand;

    float sensitivity = mapf(seg.custom3, 1, 31, 1, 10); // reduced resolution slider
    float pixVal = std::min(255.0f, volumeSmth * (float)seg.intensity / 256.0f * sensitivity);
    float intensity = mapf(pixVal, 0.0f, 255.0f, 0.0f, 100.0f) / 100.0f;  // make a brightness from the last avg

    CRGB color = 0;

    if (FFT_MajorPeak > MAX_FREQUENCY) FFT_MajorPeak = 1.0f;
    // MajorPeak holds the freq. value which is most abundant in the last sample.
    // With our sampling rate of 10240Hz we have a usable freq range from roughly 80Hz to 10240/2 Hz
    // we will treat everything with less than 65Hz as 0

    if (FFT_MajorPeak < 80) {
      color = CRGB::Black;
    } else {
      int upperLimit = 80 + 42 * seg.custom2;
      int lowerLimit = 80 + 3 * seg.custom1;
      uint8_t i =  lowerLimit!=upperLimit ? wf_map(FFT_MajorPeak, lowerLimit, upperLimit, 0, 255) : FFT_MajorPeak; // may under/overflow - so we enforce uint8_t
      unsigned b = std::min(255.0f, 255.0f * intensity);
      color = CHSV(i, 240, gamma8inv(b)); // use gamma inversion on brightness to restore pre 16.0 looks
    }

    seg.set_pixel_color(seg_len/2, color);

    // shift the pixels one pixel outwards
    // if SEGLEN equals 1 these loops won't execute
    for (unsigned i = seg_len - 1; i > seg_len/2; i--) seg.set_pixel_color(i, seg.get_pixel_color(i-1)); //move to the left
    for (unsigned i = 0; i < seg_len/2; i++)           seg.set_pixel_color(i, seg.get_pixel_color(i+1)); // move to the right
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQMATRIX
///////////////////////
//   ** Freqmatrix   //
///////////////////////
void mode_freqmatrix(Segment &seg) {        // Freqmatrix. By Andreas Pleschung.
  const unsigned seg_len = seg.length();
  // No need to prevent from executing on single led strips, we simply change pixel 0 each time and avoid the shift
  AudioData &audio = seg.audio();
  float FFT_MajorPeak = audio.fft_major_peak;
  float volumeSmth    = audio.volume_smth;

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  uint8_t secondHand = seg.now_us/(256-seg.speed)/500 % 16;
  if(seg.aux0 != secondHand) {
    seg.aux0 = secondHand;

    uint8_t sensitivity = wf_map(seg.custom3, 0, 31, 1, 10); // reduced resolution slider
    int pixVal = (volumeSmth * seg.intensity * sensitivity) / 256.0f;
    if (pixVal > 255) pixVal = 255;

    float intensity = wf_map(pixVal, 0, 255, 0, 100) / 100.0f;  // make a brightness from the last avg

    CRGB color = CRGB::Black;

    if (FFT_MajorPeak > MAX_FREQUENCY) FFT_MajorPeak = 1;
    // MajorPeak holds the freq. value which is most abundant in the last sample.
    // With our sampling rate of 10240Hz we have a usable freq range from roughly 80Hz to 10240/2 Hz
    // we will treat everything with less than 65Hz as 0

    if (FFT_MajorPeak < 80) {
      color = CRGB::Black;
    } else {
      int upperLimit = 80 + 42 * seg.custom2;
      int lowerLimit = 80 + 3 * seg.custom1;
      uint8_t i =  lowerLimit!=upperLimit ? wf_map(FFT_MajorPeak, lowerLimit, upperLimit, 0, 255) : FFT_MajorPeak;  // may under/overflow - so we enforce uint8_t
      unsigned b = 255 * intensity;
      if (b > 255) b = 255;
      color = CHSV(i, 240, gamma8inv(b)); // use gamma inversion on brightness to restore pre 16.0 looks
    }

    // shift the pixels one pixel up
    seg.set_pixel_color(0, color);
    // if SEGLEN equals 1 this loop won't execute
    for (int i = seg_len - 1; i > 0; i--) seg.set_pixel_color(i, seg.get_pixel_color(i-1)); //move to the left
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GEQ
/////////////////////////
//     ** 2D GEQ       //
/////////////////////////
void mode_2DGEQ(Segment &seg) { // By Will Tatam. Code reduction by Ewoud Wijma.
  if (!seg.is_2d()) FX_FALLBACK_STATIC; // not a 2D set-up

  const int NUM_BANDS = wf_map(seg.custom1, 0, 255, 1, 16);
  const int CENTER_BIN = wf_map(seg.custom3, 0, 31, 0, 15);
  const int cols = seg.width();
  const int rows = seg.height();

  if (!seg.allocate_data(cols*sizeof(uint16_t))) FX_FALLBACK_STATIC; //allocation failed
  uint16_t *previousBarHeight = reinterpret_cast<uint16_t*>(seg.data); //array of previous bar heights per frequency band

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  if (seg.call == 0) for (int i=0; i<cols; i++) previousBarHeight[i] = 0;

  bool rippleTime = false;
  if (seg.now - seg.step >= (256U - seg.intensity)) {
    seg.step = seg.now;
    rippleTime = true;
  }

  int fadeoutDelay = (256 - seg.speed) / 64;
  if ((fadeoutDelay <= 1 ) || ((seg.call % fadeoutDelay) == 0)) seg.fade_to_black_by(seg.speed);

  for (int x=0; x < cols; x++) {
    int band = wf_map(x, 0, cols, 0, NUM_BANDS);
    if (NUM_BANDS < 16) {
        int startBin = constrain(CENTER_BIN - NUM_BANDS/2, 0, 15 - NUM_BANDS + 1);
        if(NUM_BANDS <= 1)
          band = CENTER_BIN; // map() does not work for single band
        else
          band = wf_map(band, 0, NUM_BANDS - 1, startBin, startBin + NUM_BANDS - 1);
    }
    band = constrain(band, 0, 15);
    unsigned colorIndex = band * 17;
    int barHeight  = wf_map(fftResult[band], 0, 255, 0, rows); // do not subtract -1 from rows here
    if (barHeight > previousBarHeight[x]) previousBarHeight[x] = barHeight; //drive the peak up

    uint32_t ledColor = BLACK;
    for (int y=0; y < barHeight; y++) {
      if (seg.check1) //color_vertical / color bars toggle
        colorIndex = wf_map(y, 0, rows-1, 0, 255);

      ledColor = seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0);
      seg.set_pixel_color_xy(x, rows-1 - y, ledColor);
    }
    if (previousBarHeight[x] > 0)
      seg.set_pixel_color_xy(x, rows - previousBarHeight[x], (seg.color(2) != BLACK) ? seg.color(2) : ledColor);

    if (rippleTime && previousBarHeight[x]>0) previousBarHeight[x]--;    //delay/ripple effect
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WATERFALL
///////////////////////
//   ** Waterfall    //
///////////////////////
// Combines peak detection with FFT_MajorPeak and FFT_Magnitude.
void mode_waterfall(Segment &seg) {           // Waterfall. By: Andrew Tuline
  const unsigned seg_len = seg.length();
  // effect can work on single pixels, we just lose the shifting effect
  unsigned dataSize = sizeof(uint32_t) * seg_len;
  if (!seg.allocate_data(dataSize)) FX_FALLBACK_STATIC; //allocation failed
  uint32_t* pixels = reinterpret_cast<uint32_t*>(seg.data);

  AudioData &audio     = seg.audio();
  uint8_t samplePeak    = audio.sample_peak;
  float   FFT_MajorPeak = audio.fft_major_peak;
  uint8_t *maxVol       = &audio.max_vol;
  uint8_t *binNum       = &audio.bin_num;
  float   my_magnitude  = audio.my_magnitude / 8.0f;
  if (FFT_MajorPeak < 1) FFT_MajorPeak = 1;                                         // log10(0) is "forbidden" (throws exception)

  if (seg.call == 0) {
    for (unsigned i = 0; i < seg_len; i++) pixels[i] = BLACK;   // may not be needed as resetIfRequired() clears buffer
    seg.aux0 = 255;
    seg.custom1 = *binNum;
    seg.custom2 = *maxVol * 2;
  }

  *binNum = seg.custom1;                                  // Select a bin.
  *maxVol = seg.custom2 / 2;                              // Our volume comparator.

  uint8_t secondHand = seg.now_us / (256-seg.speed)/500 + 1 % 16;
  if (seg.aux0 != secondHand) {                           // Triggered millis timing.
    seg.aux0 = secondHand;

    //uint8_t pixCol = (log10f((float)FFT_MajorPeak) - 2.26f) * 177;  // 10Khz sampling - log10 frequency range is from 2.26 (182hz) to 3.7 (5012hz). Let's scale accordingly.
    uint8_t pixCol = (log10f(FFT_MajorPeak) - 2.26f) * 150;           // 22Khz sampling - log10 frequency range is from 2.26 (182hz) to 3.967 (9260hz). Let's scale accordingly.
    if (FFT_MajorPeak < 182.0f) pixCol = 0;                           // handle underflow

    unsigned k = seg_len-1;
    if (samplePeak) {
      pixels[k] = (uint32_t)CRGB(CHSV(92,92,gamma8inv(92))); // use gamma inversion on brightness to restore pre 16.0 looks
    } else {
      pixels[k] = color_blend(seg.color(1), seg.color_from_palette(pixCol+seg.intensity, false, seg.palette_solid_wrap(), 0), (uint8_t)my_magnitude);
    }
    seg.set_pixel_color(k, pixels[k]);
    // loop will not execute if SEGLEN equals 1
    for (unsigned i = 0; i < k; i++) {
      pixels[i] = pixels[i+1]; // shift left
      seg.set_pixel_color(i, pixels[i]);
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQPIXELS
//////////////////////
//   ** Freqpixels  //
//////////////////////
// Start frequency = 60 Hz and log10(60) = 1.78
// End frequency = 5120 Hz and lo10(5120) = 3.71
//  SEGMENT.speed select faderate
//  SEGMENT.intensity select colour index
void mode_freqpixels(Segment &seg) {        // Freqpixel. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  AudioData &audio = seg.audio();
  float FFT_MajorPeak = audio.fft_major_peak;
  float my_magnitude  = audio.my_magnitude / 16.0f;
  if (FFT_MajorPeak < 1) FFT_MajorPeak = 1.0f; // log10(0) is "forbidden" (throws exception)

  // this code translates to speed * (2 - speed/255) which is a) speed*2 or b) speed (when speed is 255)
  // and since fade_out() can only take 0-255 it will behave incorrectly when speed > 127
  //uint16_t fadeRate = 2*SEGMENT.speed - SEGMENT.speed*SEGMENT.speed/255;    // Get to 255 as quick as you can.
  unsigned fadeRate = seg.speed*seg.speed; // Get to 255 as quick as you can.
  fadeRate = wf_map(fadeRate, 0, 65535, 1, 255);

  int fadeoutDelay = (256 - seg.speed) / 64;
  if ((fadeoutDelay <= 1 ) || ((seg.call % fadeoutDelay) == 0)) seg.fade_out(fadeRate);

  uint8_t pixCol = (log10f(FFT_MajorPeak) - 1.78f) * 255.0f/(MAX_FREQ_LOG10 - 1.78f);  // Scale log10 of frequency values to the 255 colour index.
  if (FFT_MajorPeak < 61.0f) pixCol = 0;                                               // handle underflow
  for (int i=0; i < seg.intensity/32+1; i++) {
    unsigned locn = hw_random16(0,seg_len);
    seg.set_pixel_color(locn, color_blend(seg.color(1), seg.color_from_palette(seg.intensity+pixCol, false, seg.palette_solid_wrap(), 0), (uint8_t)my_magnitude));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEMOVE
//////////////////////
//   ** Noisemove   //
//////////////////////
void mode_noisemove(Segment &seg) {         // Noisemove.    By: Andrew Tuline
  const unsigned seg_len = seg.length();
  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  int fadeoutDelay = (256 - seg.speed) / 96;
  if ((fadeoutDelay <= 1 ) || ((seg.call % fadeoutDelay) == 0)) seg.fade_to_black_by(4+ seg.speed/4);

  uint8_t numBins = wf_map(seg.intensity,0,255,0,16);     // Map slider to fftResult bins.
  for (int i=0; i<numBins; i++) {                         // How many active bins are we using.
    unsigned locn = perlin16(seg.now*seg.speed+i*50000, seg.now*seg.speed);   // Get a new pixel location from moving noise.
    // if SEGLEN equals 1 locn will be always 0, hence we set the first pixel only
    locn = wf_map(locn, 7500, 58000, 0, seg_len-1);       // Map that to the length of the strand, and ensure we don't go over.
    seg.set_pixel_color(locn, color_blend(seg.color(1), seg.color_from_palette(i*64, false, seg.palette_solid_wrap(), 0), uint8_t(fftResult[i % 16]*4)));
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQMAP
////////////////////
//   ** Freqmap   //
////////////////////
void mode_freqmap(Segment &seg) {           // Map FFT_MajorPeak to SEGLEN. Would be better if a higher framerate.
  const unsigned seg_len = seg.length();
  if (seg_len <= 1) FX_FALLBACK_STATIC;
  // Start frequency = 60 Hz and log10(60) = 1.78
  // End frequency = MAX_FREQUENCY in Hz and lo10(MAX_FREQUENCY) = MAX_FREQ_LOG10

  AudioData &audio = seg.audio();
  float FFT_MajorPeak = audio.fft_major_peak;
  float my_magnitude  = audio.my_magnitude / 4.0f;
  if (FFT_MajorPeak < 1) FFT_MajorPeak = 1;                                         // log10(0) is "forbidden" (throws exception)

  if (seg.call == 0) seg.fill(BLACK);
  int fadeoutDelay = (256 - seg.speed) / 32;
  if ((fadeoutDelay <= 1 ) || ((seg.call % fadeoutDelay) == 0)) seg.fade_out(seg.speed);

  int locn = (log10f((float)FFT_MajorPeak) - 1.78f) * (float)seg_len/(MAX_FREQ_LOG10 - 1.78f);  // log10 frequency range is from 1.78 to 3.71. Let's scale to SEGLEN.
  if (locn < 1) locn = 0; // avoid underflow

  if (locn >= (int)seg_len) locn = seg_len-1;
  unsigned pixCol = (log10f(FFT_MajorPeak) - 1.78f) * 255.0f/(MAX_FREQ_LOG10 - 1.78f);   // Scale log10 of frequency values to the 255 colour index.
  if (FFT_MajorPeak < 61.0f) pixCol = 0;                                                 // handle underflow

  uint8_t bright = (uint8_t)my_magnitude;

  seg.set_pixel_color(locn, color_blend(seg.color(1), seg.color_from_palette(seg.intensity+pixCol, false, seg.palette_solid_wrap(), 0), bright));
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DJ_LIGHT
/////////////////////////
//   ** DJLight        //
/////////////////////////
void mode_DJLight(Segment &seg) {           // Written by ??? Adapted by Will Tatam.
  const unsigned seg_len = seg.length();
  // No need to prevent from executing on single led strips, only mid will be set (mid = 0)
  const int mid = seg_len / 2;

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  uint8_t secondHand = seg.now_us/(256-seg.speed)/500+1 % 64;
  if (seg.aux0 != secondHand) {                           // Triggered millis timing.
    seg.aux0 = secondHand;

    CRGB color = CRGB(gamma8inv(fftResult[15]/2), gamma8inv(fftResult[5]/2), gamma8inv(fftResult[0]/2)); // apply gamma inversion to restor pre 16.0 looks
    seg.set_pixel_color(mid, color.fadeToBlackBy(wf_map(fftResult[4], 0, 255, 255, 4)));     // TODO - Update

    // if SEGLEN equals 1 these loops won't execute
    for (int i = seg_len - 1; i > mid; i--)   seg.set_pixel_color(i, seg.get_pixel_color(i-1)); // move to the left
    for (int i = 0; i < mid; i++)             seg.set_pixel_color(i, seg.get_pixel_color(i+1)); // move to the right
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FUNKY_PLANK
/////////////////////////
//  ** 2D Funky plank  //
/////////////////////////
void mode_2DFunkyPlank(Segment &seg) {      // Written by ??? Adapted by Will Tatam.
  if (!seg.is_2d()) FX_FALLBACK_STATIC; // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  int NUMB_BANDS = wf_map(seg.custom1, 0, 255, 1, 16);
  int barWidth = (cols / NUMB_BANDS);
  int bandInc = 1;
  if (barWidth == 0) {
    // Matrix narrower than fft bands
    barWidth = 1;
    bandInc = (NUMB_BANDS / cols);
  }

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  uint8_t secondHand = seg.now_us/(256-seg.speed)/500+1 % 64;
  if (seg.aux0 != secondHand) {                           // Triggered millis timing.
    seg.aux0 = secondHand;

    // display values of
    int b = 0;
    for (int band = 0; band < NUMB_BANDS; band += bandInc, b++) {
      int hue = fftResult[band % 16];
      int v = wf_map(fftResult[band % 16], 0, 255, 10, 255);
      for (int w = 0; w < barWidth; w++) {
         int xpos = (barWidth * b) + w;
         seg.set_pixel_color_xy(xpos, 0, CHSV(hue, 255, gamma8inv(v))); // use gamma inversion on brightness to restore original pre 16.0 looks
      }
    }

    // Update the display:
    for (int i = (rows - 1); i > 0; i--) {
      for (int j = (cols - 1); j >= 0; j--) {
        seg.set_pixel_color_xy(j, i, seg.get_pixel_color_xy(j, i-1));
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLURZ
//////////////////////
//    ** Blurz      //
//////////////////////

// WLED's FX.h macro, which reads SEGMENT.speed and SEGLEN. Those are seg.speed and
// the local seg_len here, so it stays a macro and the body stays verbatim.

void mode_blurz(Segment &seg) {            // Blurz. By Andrew Tuline.
  const unsigned seg_len = seg.length();
  if (seg_len <= 1) FX_FALLBACK_STATIC;
  // even with 1D effect we have to take logic for 2D segments for allocation as fill_solid() fills whole segment

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  if (seg.call == 0) {
    seg.fill(BLACK);
    seg.aux0 = 0;
  }

  int fadeoutDelay = (256 - seg.speed) / 32;
  if ((fadeoutDelay <= 1 ) || ((seg.call % fadeoutDelay) == 0)) seg.fade_out(seg.speed);

  seg.step += FRAMETIME;
  if (seg.step > speed_formula_l(seg, seg_len)) {
    unsigned segLoc = hw_random16(seg_len);
    seg.set_pixel_color(segLoc, color_blend(seg.color(1), seg.color_from_palette(2*fftResult[seg.aux0%16]*240/std::max(1, (int)seg_len-1), false, seg.palette_solid_wrap(), 0), uint8_t(2*fftResult[seg.aux0%16])));
    ++(seg.aux0) %= 16; // make sure it doesn't cross 16

    seg.step = 1;
    seg.blur(seg.intensity); // note: blur > 210 results in a alternating pattern, this could be fixed by mapping but some may like it (very old bug)
  }
}

#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROCKTAVES
//////////////////////
//   ** Rocktaves   //
//////////////////////
void mode_rocktaves(Segment &seg) {         // Rocktaves. Same note from each octave is same colour.    By: Andrew Tuline
  const unsigned seg_len = seg.length();
  AudioData &audio = seg.audio();
  float   FFT_MajorPeak = audio.fft_major_peak;
  float   my_magnitude  = audio.my_magnitude / 16.0f;

  seg.fade_to_black_by(16);                               // Just in case something doesn't get faded.

  float frTemp = FFT_MajorPeak;
  uint8_t octCount = 0;                                   // Octave counter.
  uint8_t volTemp = 0;

  volTemp = 32.0f + my_magnitude * 1.5f;                  // brightness = volume (overflows are handled in next lines)
  if (my_magnitude < 48) volTemp = 0;                     // We need to squelch out the background noise.
  if (my_magnitude > 144) volTemp = 255;                  // everything above this is full brightness

  while ( frTemp > 249 ) {
    octCount++;                                           // This should go up to 5.
    frTemp = frTemp/2;
  }

  frTemp -= 132.0f;                                       // This should give us a base musical note of C3
  frTemp  = fabsf(frTemp * 2.1f);                         // Fudge factors to compress octave range starting at 0 and going to 255;

  unsigned i = wf_map(beatsin8_t(8+octCount*4, 0, 255, seg.now, 0, octCount*8), 0, 255, 0, seg_len-1);
  i = constrain(i, 0U, seg_len-1U);
  seg.add_pixel_color(i, color_blend(seg.color(1), seg.color_from_palette((uint8_t)frTemp, false, seg.palette_solid_wrap(), 0), volTemp));
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_AKEMI
/////////////////////////
//     2D Akemi        //
/////////////////////////
const uint8_t akemi[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,2,2,3,3,3,3,3,3,2,2,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,2,3,3,0,0,0,0,0,0,3,3,2,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,2,3,0,0,0,6,5,5,4,0,0,0,3,2,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,2,3,0,0,6,6,5,5,5,5,4,4,0,0,3,2,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,2,3,0,6,5,5,5,5,5,5,5,5,4,0,3,2,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,2,3,0,6,5,5,5,5,5,5,5,5,5,5,4,0,3,2,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,3,2,0,6,5,5,5,5,5,5,5,5,5,5,4,0,2,3,0,0,0,0,0,0,0,
  0,0,0,0,0,0,3,2,3,6,5,5,7,7,5,5,5,5,7,7,5,5,4,3,2,3,0,0,0,0,0,0,
  0,0,0,0,0,2,3,1,3,6,5,1,7,7,7,5,5,1,7,7,7,5,4,3,1,3,2,0,0,0,0,0,
  0,0,0,0,0,8,3,1,3,6,5,1,7,7,7,5,5,1,7,7,7,5,4,3,1,3,8,0,0,0,0,0,
  0,0,0,0,0,8,3,1,3,6,5,5,1,1,5,5,5,5,1,1,5,5,4,3,1,3,8,0,0,0,0,0,
  0,0,0,0,0,2,3,1,3,6,5,5,5,5,5,5,5,5,5,5,5,5,4,3,1,3,2,0,0,0,0,0,
  0,0,0,0,0,0,3,2,3,6,5,5,5,5,5,5,5,5,5,5,5,5,4,3,2,3,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,6,5,5,5,5,5,7,7,5,5,5,5,5,4,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,6,5,5,5,5,5,5,5,5,5,5,5,5,4,0,0,0,0,0,0,0,0,0,
  1,0,0,0,0,0,0,0,0,6,5,5,5,5,5,5,5,5,5,5,5,5,4,0,0,0,0,0,0,0,0,2,
  0,2,2,2,0,0,0,0,0,6,5,5,5,5,5,5,5,5,5,5,5,5,4,0,0,0,0,0,2,2,2,0,
  0,0,0,3,2,0,0,0,6,5,4,4,4,4,4,4,4,4,4,4,4,4,4,4,0,0,0,2,2,0,0,0,
  0,0,0,3,2,0,0,0,6,5,5,5,5,5,5,5,5,5,5,5,5,5,5,4,0,0,0,2,3,0,0,0,
  0,0,0,0,3,2,0,0,0,0,3,3,0,3,3,0,0,3,3,0,3,3,0,0,0,0,2,2,0,0,0,0,
  0,0,0,0,3,2,0,0,0,0,3,2,0,3,2,0,0,3,2,0,3,2,0,0,0,0,2,3,0,0,0,0,
  0,0,0,0,0,3,2,0,0,3,2,0,0,3,2,0,0,3,2,0,0,3,2,0,0,2,3,0,0,0,0,0,
  0,0,0,0,0,3,2,2,2,2,0,0,0,3,2,0,0,3,2,0,0,0,3,2,2,2,3,0,0,0,0,0,
  0,0,0,0,0,0,3,3,3,0,0,0,0,3,2,0,0,3,2,0,0,0,0,3,3,3,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void mode_2DAkemi(Segment &seg) {
  if (!seg.is_2d()) FX_FALLBACK_STATIC; // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  unsigned counter = (seg.now * ((seg.speed >> 2) +2)) & 0xFFFF;
  counter = counter >> 8;

  const float lightFactor  = 0.15f;
  const float normalFactor = 0.4f;

  // Upstream fetches um_data here and falls back to simulateSound() itself, then
  // guards on the pointers being non-null. seg.audio() already does the fallback
  // and fft_result is an array, so both guards are gone.
  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;
  float base = fftResult[0]/255.0f;

  //draw and color Akemi
  for (int y=0; y < rows; y++) for (int x=0; x < cols; x++) {
    CRGB color;
    CRGB soundColor = CRGB::Orange;
    CRGB faceColor  = CRGB(seg.color_wheel(counter));
    CRGB armsAndLegsColor = CRGB(seg.color(1) > 0 ? seg.color(1) : 0xFFE0A0); //default warmish white 0xABA8FF; //0xFF52e5;//
    uint8_t ak = pgm_read_byte_near(akemi + ((y * 32)/rows) * 32 + (x * 32)/cols); // akemi[(y * 32)/rows][(x * 32)/cols]
    switch (ak) {
      case 3: armsAndLegsColor.r *= lightFactor;  armsAndLegsColor.g *= lightFactor;  armsAndLegsColor.b *= lightFactor;  color = armsAndLegsColor; break; //light arms and legs 0x9B9B9B
      case 2: armsAndLegsColor.r *= normalFactor; armsAndLegsColor.g *= normalFactor; armsAndLegsColor.b *= normalFactor; color = armsAndLegsColor; break; //normal arms and legs 0x888888
      case 1: color = armsAndLegsColor; break; //dark arms and legs 0x686868
      case 6: faceColor.r *= lightFactor;  faceColor.g *= lightFactor;  faceColor.b *= lightFactor;  color=faceColor; break; //light face 0x31AAFF
      case 5: faceColor.r *= normalFactor; faceColor.g *= normalFactor; faceColor.b *= normalFactor; color=faceColor; break; //normal face 0x0094FF
      case 4: color = faceColor; break; //dark face 0x007DC6
      case 7: color = seg.color(2) > 0 ? seg.color(2) : 0xFFFFFF; break; //eyes and mouth default white
      case 8: if (base > 0.4) {soundColor.r *= base; soundColor.g *= base; soundColor.b *= base; color=soundColor;} else color = armsAndLegsColor; break;
      default: color = BLACK; break;
    }

    if (seg.intensity > 128 && fftResult[0] > 128) { //dance if base is high
      seg.set_pixel_color_xy(x, 0, BLACK);
      seg.set_pixel_color_xy(x, y+1, color);
    } else
      seg.set_pixel_color_xy(x, y, color);
  }

  //add geq left and right
  {
    int xMax = cols/8;
    for (int x=0; x < xMax; x++) {
      unsigned band = wf_map(x, 0, std::max(xMax,4), 0, 15);  // map 0..cols/8 to 16 GEQ bands
      band = constrain(band, 0, 15);
      int barHeight = wf_map(fftResult[band], 0, 255, 0, 17*rows/32);
      uint32_t color = seg.color_from_palette((band * 35), false, seg.palette_solid_wrap(), 0);

      for (int y=0; y < barHeight; y++) {
        seg.set_pixel_color_xy(x, rows/2-y, color);
        seg.set_pixel_color_xy(cols-1-x, rows/2-y, color);
      }
    }
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GRAVFREQ
    {"Gravfreq@Rate of fall,Sensitivity;!,!;!;1f;ix=128,m12=0,si=0", mode_gravfreq},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQWAVE
    {"Freqwave@Speed,Sound effect,Low bin,High bin,Pre-amp;;;01f;m12=2,si=0", mode_freqwave},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQMATRIX
    {"Freqmatrix@Speed,Sound effect,Low bin,High bin,Sensitivity;;;01f;m12=3,si=0", mode_freqmatrix},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GEQ
    {"GEQ@Fade speed,Ripple decay,# of bands,,Bin,Color bars;!,,Peaks;!;2f;c1=255,c2=64,pal=11,si=0,c3=0", mode_2DGEQ},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WATERFALL
    {"Waterfall@!,Adjust color,Select bin,Volume (min);!,!;!;01f;c2=0,m12=2,si=0", mode_waterfall},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQPIXELS
    {"Freqpixels@Fade rate,Starting color and # of pixels;!,!,;!;1f;m12=0,si=0", mode_freqpixels},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISEMOVE
    {"Noisemove@Move speed,Fade rate;!,!;!;01f;m12=0,si=0", mode_noisemove},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FREQMAP
    {"Freqmap@Fade rate,Starting color;!,!;!;1f;m12=0,si=0", mode_freqmap},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DJ_LIGHT
    {"DJ Light@Speed;;;01f;m12=2,si=0", mode_DJLight},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FUNKY_PLANK
    {"Funky Plank@Scroll speed,,# of bands;;;2f;si=0", mode_2DFunkyPlank},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLURZ
    {"Blurz@Fade rate,Blur;!,Color mix;!;1f;m12=0,si=0", mode_blurz},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROCKTAVES
    {"Rocktaves@;!,!;!;01f;m12=1,si=0", mode_rocktaves},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_AKEMI
    {"Akemi@Color speed,Dance;Head palette,Arms & Legs,Eyes & Mouth;Face palette;2f;si=0", mode_2DAkemi},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_AUDIO_FFT;
const EffectGroup EFFECT_GROUP_AUDIO_FFT{"audio_fft", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_AUDIO_FFT
