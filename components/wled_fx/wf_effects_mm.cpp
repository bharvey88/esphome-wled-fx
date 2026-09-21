/* Effect bodies ported from WLED-MM (MoonModules) wled00/FX.cpp with the
 * mechanical transform described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Copyright (c) 2022-present the MoonModules WLED-MM contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to. GEQ 3D and Paintbrush
 * carry a separate GPLv3 notice upstream, reproduced above each of them below.
 */

/* MoonModules WLED-MM exclusives. Bodies come from refs/WLED-MM,
 * not refs/WLED, and GEQ 3D and Paintbrush carry their own licence notice, which
 * is reproduced above each of those two effects. See BATCHES.md.
 *
 * Source: WLED-MM branch mdev, commit 272dab5939d6d83a7c9a6e31d0a2a628c2d284e0.
 *
 * MM is still on the 0.15 engine, so three MM-only conventions are transformed
 * away on the way in, on top of the table in PORTING.md section 3:
 *
 *   uint16_t mode_x() ... return FRAMETIME;   ->  void mode_x(Segment &seg)
 *   return mode_oops();                       ->  FX_FALLBACK_STATIC
 *   SEGMENT.setUpLeds()                       ->  dropped
 *
 * `setUpLeds()` asks MM for a per-segment leds[] array so a later getPixelColor()
 * reads back losslessly. This engine's canvas is already a lossless uint32_t
 * framebuffer, so the read-back MM wants is simply what seg.get_pixel_color*()
 * does, and the call has nothing left to do.
 *
 * MM's metadata strings are copied verbatim except for two things the registry
 * here cannot carry: the trailing moon glyph MM puts on its own effects (the name
 * is the registry key, the YAML key and the select option text, and a non-ASCII
 * key is awkward in all three), and MM's m12 numbering, where Pinwheel is 7 and
 * here it is 4. Both edits are called out on the entry that needed them.
 */

// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_MM \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METEOR_SMOOTH || WLED_FX_FX_PARTY_JERK || WLED_FX_FX_POPCORN_AUDIO || \
   WLED_FX_FX_MULTI_COMET_AUDIO || WLED_FX_FX_FW_STARBURST_AUDIO || WLED_FX_FX_FIREWORKS_AUDIO || \
   WLED_FX_FX_GEQ_3D || WLED_FX_FX_PAINTBRUSH || WLED_FX_FX_SNOW_FALL)

#if WLED_FX_GROUP_MM

namespace esphome {
namespace wled_fx {
namespace {

// ---------------------------------------------------------------------------
// Helpers MM has and this engine does not. Copied into this translation unit as
// static functions, per PORTING.md section 8.
// ---------------------------------------------------------------------------

/* FastLED lib8tion map8(). MM's FX.cpp gets it from FastLED; nothing in wf_math.h
 * provides it, so it lives here. Scales `in` into [rangeStart, rangeEnd]. */
uint8_t map8(uint8_t in, uint8_t rangeStart, uint8_t rangeEnd) {
  uint8_t rangeWidth = rangeEnd - rangeStart;
  uint8_t out = scale8(in, rangeWidth);
  out += rangeStart;
  return out;
}

/* WLED-MM FX.cpp:102. More accurate integer version of map() - based on map3()
 * proposed in https://forum.arduino.cc/t/how-map-loses-precision-and-how-to-fix-it/371026/3
 * rounding instead of truncation, better handling of inverted ranges
 * Important: don't use when the input range is very small, because the output is
 * such cases is worse than map() */
long map2(long x, long in_min, long in_max, long out_min, long out_max) {
  long out_range = out_max - out_min;
  if (out_range > 0)
    out_range++;
  else if (out_range < 0)
    out_range--;
  else
    return out_min;  // output range is 0

  long in_range = in_max - in_min;
  if (in_range > 0)
    in_range++;
  else if (in_range < 0)
    in_range--;
  else
    return out_min;  // input range is 0

  return ((x - in_min) * out_range) / in_range + out_min;
}

/* WLED-MM FX_2Dfcn.cpp:726. MM's Segment::drawLine() carries a `depth` argument
 * that upstream 16.0.1 does not have, and GEQ 3D and Paintbrush are built on it:
 * it shortens the line towards its start point before drawing, which is what
 * turns a fan of lines into a perspective projection. The shortening is copied
 * verbatim from MM and the shortened line then goes through this engine's own
 * draw_line(), which is otherwise the same Bresenham and Xiaolin Wu code. */
void draw_line_depth(const Segment &seg, int x0, int y0, int x1, int y1, uint32_t c, bool soft, uint8_t depth) {
  // WLEDMM shorten line according to depth
  if (depth < UINT8_MAX) {
    if (depth == 0)
      return;  // nothing to paint
    if (depth < 2) {
      x1 = x0;
      y1 = y0;
    }       // single pixel
    else {  // shorten line
      x0 *= 2;
      y0 *= 2;  // we do everything "*2" for better rounding
      int dx1 = ((2 * x1 - x0) * int(depth)) / 255;  // X distance, scaled down by depth
      int dy1 = ((2 * y1 - y0) * int(depth)) / 255;  // Y distance, scaled down by depth
      x1 = (x0 + dx1 + 1) / 2;
      y1 = (y0 + dy1 + 1) / 2;
      x0 /= 2;
      y0 /= 2;
    }
  }
  seg.draw_line(x0, y0, x1, y1, c, soft);
}

void draw_line_depth(const Segment &seg, int x0, int y0, int x1, int y1, CRGB c, bool soft, uint8_t depth) {
  draw_line_depth(seg, x0, y0, x1, y1, RGBW32(c.r, c.g, c.b, 0), soft, depth);
}

// ---------------------------------------------------------------------------
// Meteor Smooth
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METEOR_SMOOTH
/* meteor effect & meteor smooth (merged by @dedehai)
 * send a meteor from begining to to the end of the strip with a trail that
 * randomly decays.
 * adapted from
 * https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/#LEDStripEffectMeteorRain
 *
 * Meteor Smooth is not an MM invention: upstream 16.0.1 still ships the merged
 * core but comments the second registration out ("merged with mode_meteor"), and
 * MM keeps it registered at ID 77. MM's core is what is copied here, and only the
 * smooth entry is registered from it; plain Meteor stays in the 1d_c batch. */
void mode_meteor_core(Segment &seg, bool smooth) {
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;
  if (!seg.allocate_data(seg_len))
    FX_FALLBACK_STATIC;  // allocation failed
  const bool meteorSmooth = smooth || seg.check3;
  uint8_t *trail = seg.data;

  const unsigned meteorSize = 1 + seg_len / 20;  // 5%
  unsigned meteorstart;
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
    int index = (meteorstart + j) % seg_len;
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

void mode_meteor_smooth(Segment &seg) { mode_meteor_core(seg, true); }
#endif

// ---------------------------------------------------------------------------
// Party jerk
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PARTY_JERK
//       PARTYJERK        //
// by  @tonyxforce
// NB: This effects expects a palette that starts with black and then ramps up brightness.
//     Currently works best with the "color gradient" and the "colors 1&2" palettes
void mode_partyjerk(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg.call == 0) {
    seg.fill(BLACK);  // clear LEDs
    seg.aux0 = 0;
    seg.aux1 = 0;
    seg.step = 0;
  }
  /*
   * use of persistent variables:
   * aux0: hueDelay
   * aux1: hue
   * step: pos
   */

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;

  seg.aux0++;
  if (seg.aux1 > 254) {
    seg.aux1 = 0;
  }
  if (seg.aux0 > map2(seg.custom1, 0, 255, 0, 14)) {
    seg.aux0 = 0;
    seg.aux1++;
  }

  uint_fast32_t speed = 0;
  uint16_t counter = 0;

  if (volumeSmth * 2 > (255 - seg.intensity)) {
    speed = seg.speed * map2(seg.custom2, 0, 255, 0, 100);
  } else {
    speed = seg.speed;
  }

  seg.step += speed;
  counter = seg.step >> 8;

  for (unsigned i = 0; i < seg_len; i++) {
    uint8_t colorIndex = ((i * 255) / seg_len) - counter;
    uint32_t paletteColor = seg.color_from_palette(colorIndex, false, seg.palette_moving_wrap(), 255);
    uint8_t r = R(paletteColor);
    uint8_t g = G(paletteColor);
    uint8_t b = B(paletteColor);
    uint8_t activeColor = std::max(r, std::max(g, b));

    CRGB rgb(CHSV(seg.aux1, 255, activeColor));
    seg.set_pixel_color(static_cast<int>(i), rgb.r, rgb.g, rgb.b);
  }
}  // mode_partyjerk()
#endif

// ---------------------------------------------------------------------------
// Popcorn audio
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POPCORN_AUDIO
constexpr unsigned maxNumPopcorn = 21;  // max 21 on 16 segment ESP8266
/*
 *  POPCORN
 *  modified from https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/Popcorn.h
 *
 * MM's core is not upstream 16.0.1's mode_popcorn with an extra branch: it is
 * delta-time paced, has a different gravity constant, draws a three pixel trail
 * and sizes its allocation differently. Copying the whole core is therefore the
 * only way to get MM's audio variant; plain Popcorn stays in the 1d_d batch,
 * untouched, on the upstream body.
 */
void mode_popcorn_core(Segment &seg, bool useaudio) {
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;
  // allocate segment data
  unsigned strips = seg.nr_of_v_strips();
  size_t dataSize = sizeof(spark) * maxNumPopcorn;
  unsigned neededPopcorn = maxNumPopcorn;  // WLEDMM
  if (strips > 8) {  // WLEDMM more than 8 virtual strips --> reduce memory requirements to minimum necessary
    neededPopcorn = (seg.intensity * maxNumPopcorn) / 255;
    neededPopcorn = std::min(std::max(neededPopcorn, 2u), maxNumPopcorn);
    dataSize = sizeof(spark) * neededPopcorn;
  }
  if (!seg.allocate_data(dataSize * strips))
    FX_FALLBACK_STATIC;  // allocation failed

  Spark *popcorn = reinterpret_cast<Spark *>(seg.data);

  if (seg.call == 0) {
    seg.fill(BLACK);   // WLEDMM clear LEDs at startup
    seg.step = seg.now;  // initial time
  }

  bool hasCol2 = seg.color(2);
  if (!seg.check2)
    seg.fill(hasCol2 ? BLACK : seg.color(1));

  // WLEDMM init um_data. MM falls back to non-audio behaviour when the usermod is
  // absent; here seg.audio() is always answerable, from the simulation when no
  // microphone is attached, so the audio path always runs.
  AudioData &audio = seg.audio();

  const auto run_strip = [&](uint16_t stripNr, Spark *popcorn, float deltaTime) {
    float gravity = -0.0001f - (seg.speed / 180000.0f);  // m/s/s
                                                         // WLEDMM original value was "-0.0001f - (SEGMENT.speed/200000.0f)"
    gravity *= std::min(std::max(1, int(seg_len) - 1), 255);  // WLEDMM speed limit 255

    unsigned numPopcorn = seg.intensity * maxNumPopcorn / 255;
    if (numPopcorn == 0)
      numPopcorn = 1;
    // WLEDMM audioreactive vars
    float volumeSmth = audio.volume_smth;
    int16_t volumeRaw = static_cast<int16_t>(audio.volume_raw);
    uint8_t samplePeak = audio.sample_peak;

    for (unsigned i = 0; i < numPopcorn; i++) {
      if (popcorn[i].pos >= 0.0f) {  // if kernel is active, update its position
        popcorn[i].pos += popcorn[i].vel * deltaTime;
        popcorn[i].vel += gravity * deltaTime;
      } else {  // if kernel is inactive, randomly pop it
        bool doPopCorn = false;  // WLEDMM allows to inhibit new pops
        // WLEDMM begin
        if (useaudio) {
          if ((volumeSmth > 1.0f)                                    // no pops in silence
              && ((samplePeak > 0) || (volumeRaw > 128))             // try to pop at onsets
              && (hw_random8() < 4))                                 // stay somewhat random
            doPopCorn = true;
        } else {
          if (hw_random8() < 2)
            doPopCorn = true;  // default POP!!!
        }
        // WLEDMM end

        if (doPopCorn) {  // POP!!!
          popcorn[i].pos = 0.01f;

          unsigned peakHeight = 128 + hw_random8(128);  // 0-255
          peakHeight = (peakHeight * (seg_len - 1)) >> 8;
          popcorn[i].vel = sqrtf(-2.01f * gravity * peakHeight);

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
        // WLEDMM add small trail
        for (int n = 1; n < 4; n++) {
          float spdLimit = n;
          unsigned fade = 128 - 32 * n;
          uint32_t trailColor = color_fade(col, fade, true);
          if ((popcorn[i].vel < -spdLimit) && (ledIndex + n < seg_len))
            seg.set_pixel_color(Segment::index_to_v_strip(ledIndex + n, stripNr), trailColor);
          if ((popcorn[i].vel > spdLimit) && (ledIndex >= unsigned(n)))
            seg.set_pixel_color(Segment::index_to_v_strip(ledIndex - n, stripNr), trailColor);
        }
      }
    }
  };

  // WLEDMM calculate time passed
  uint32_t millisPassed = std::min(std::max(1u, unsigned(seg.now - seg.step)), 200u);  // constrain between 1 and 200
  seg.step = seg.now;
  // base speed: 64 FPS (normal) / 120fps (audioreactive)
  float deltaTime = useaudio ? float(millisPassed) / 8.0f : float(millisPassed) / 16.0f;
  for (unsigned stripNr = 0; stripNr < strips; stripNr++)
    run_strip(stripNr, &popcorn[stripNr * neededPopcorn], deltaTime);
}

void mode_popcorn_audio(Segment &seg) { mode_popcorn_core(seg, true); }
#endif

// ---------------------------------------------------------------------------
// Multi Comet audio
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MULTI_COMET_AUDIO
// audioreactive multi-comet by @softhack007
void mode_multi_comet_ar(Segment &seg) {
  const unsigned seg_len = seg.length();
  constexpr unsigned MAX_COMETS = 16;  // was 8
  uint32_t cycleTime = std::max(1, int((255 - seg.speed) / 4));
  uint32_t it = seg.now / cycleTime;
  if (seg.step == it)
    return;  // too early

  if (!seg.allocate_data(sizeof(uint16_t) * MAX_COMETS))
    FX_FALLBACK_STATIC;  // allocation failed
  uint16_t *comets = reinterpret_cast<uint16_t *>(seg.data);
  if (seg.call == 0) {  // do some initializations
    seg.fill(BLACK);
    for (unsigned i = 0; i < MAX_COMETS; i++)
      comets[i] = seg_len;  // WLEDMM make sure comments are started individually
    seg.aux0 = 0;
  }
  seg.fade_out(254 - seg.intensity / 2);

  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;
  int16_t volumeRaw = static_cast<int16_t>(audio.volume_raw);
  uint8_t samplePeak = audio.sample_peak;

  uint16_t armed = seg.aux0;  // allows to delay comet launch

  bool shotOne = false;  // avoids starting several coments at the same time (invisible due to overlap)
  for (unsigned i = 0; i < MAX_COMETS; i++) {
    if (comets[i] < seg_len) {
      // draw comet
      uint16_t index = comets[i];
      if (seg.color(2) != 0)
        seg.set_pixel_color(index,
                            i % 2 ? seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0) : seg.color(2));
      else
        seg.set_pixel_color(index, seg.color_from_palette(index, true, seg.palette_solid_wrap(), 0));
      comets[i]++;  // move
    } else {
      // randomly launch a new comet
      if (hw_random16(std::min(256u, seg_len)) < 3)
        armed++;  // new comet loaded and ready
      if (armed > 2)
        armed = 2;  // max three armed at once (avoid overlap)
      // delay comet "launch" during silence, and wait until next beat
      if ((armed > 0) && (shotOne == false) && (volumeSmth > 1.0f) &&
          ((samplePeak > 0) || (volumeRaw > 104))) {  // delayed lauch - wait until peak, don't launch in silence
        comets[i] = 0;                                // start a new comet!
        armed--;                                      // un-arm one
        shotOne = true;
      }
    }
  }
  seg.aux0 = armed;
  seg.step = it;
}
#endif

// ---------------------------------------------------------------------------
// Fw Starburst audio
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FW_STARBURST_AUDIO
/*
/ Fireworks in starburst effect
/ based on the video: https://www.reddit.com/r/arduino/comments/c3sd46/i_made_this_fireworks_effect_for_my_led_strips/
/ Speed sets frequency of new starbursts, intensity is the intensity of the burst
*/
#define STARBURST_MAX_FRAG 10  // 60 bytes / star
// each needs 20+STARBURST_MAX_FRAG*4 bytes
typedef struct particle {
  CRGB color;
  uint32_t birth = 0;
  uint32_t last = 0;
  float vel = 0;
  uint16_t pos = -1;
  float fragment[STARBURST_MAX_FRAG];
} star;

void mode_starburst_core(Segment &seg, bool useaudio) {
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;
  unsigned maxData = FAIR_DATA_PER_SEG;  // ESP8266: 256 ESP32: 640
  unsigned segs = strip_active_segments_num();
  if (segs <= (strip_max_segments() / 2))
    maxData *= 2;  // ESP8266: 512 if <= 8 segs ESP32: 1280 if <= 16 segs
  if (segs <= (strip_max_segments() / 4))
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

  // WLEDMM init um_data. Always answerable here, see the note on Popcorn audio.
  AudioData &audio = seg.audio();
  float volumeSmth = audio.volume_smth;
  int16_t volumeRaw = static_cast<int16_t>(audio.volume_raw);
  uint8_t samplePeak = audio.sample_peak;

  for (unsigned j = 0; j < numStars; j++) {
    // speed to adjust chance of a burst, max is nearly always.
    bool doNewStar = hw_random8((144 - (seg.speed >> 1))) == 0;  // WLEDMM original spawning trigger
    // WLEDMM begin
    if (useaudio) {
      doNewStar = false;
      int burstplus = (volumeSmth > 159) ? 96 : 0;  // high volume -> more stars
      if (volumeRaw <= 56)
        burstplus = -64;  // low volume  -> fewer stars
      int birthrate = (144 - (seg.speed >> 1)) - burstplus;
      birthrate = constrain(birthrate, 4, 144);
      if ((volumeSmth > 1.0f)                           // no bursts in silence
          && ((samplePeak > 0) || (volumeRaw > 48))     // try to burst with sound
          && (hw_random8(birthrate) == 0))              // original random rate
        doNewStar = true;
    }
    // WLEDMM end

    if (doNewStar && stars[j].birth == 0)  // WLEDMM
    {
      // Pick a random color and location.
      unsigned startPos = (seg_len > 1) ? hw_random16(seg_len - 1) : 0;
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
        uint8_t f = 254.5f * fade;
        c = CRGB(color_blend(RGBW32(c.r, c.g, c.b, 0), seg.color(1), f));
      }
    }

    float particleSize = (1.0f - fade) * 2.0f;

    for (size_t index = 0; index < STARBURST_MAX_FRAG * 2; index++) {
      bool mirrored = index & 0x1;
      uint8_t i = index >> 1;
      if (stars[j].fragment[i] > 0) {
        float loc = stars[j].fragment[i];
        if (mirrored)
          loc -= (loc - stars[j].pos) * 2;
        int start = loc - particleSize;
        int end = loc + particleSize;
        if (start < 0)
          start = 0;
        if (start == end)
          end++;
        if (end > int(seg_len))
          end = seg_len;
        for (int p = start; p < end; p++) {
          seg.set_pixel_color(p, c.r, c.g, c.b);
        }
      }
    }
  }
}
#undef STARBURST_MAX_FRAG

void mode_starburst_audio(Segment &seg) { mode_starburst_core(seg, true); }
#endif

// ---------------------------------------------------------------------------
// Fireworks audio
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_AUDIO
void mode_fireworks_core(Segment &seg, bool useaudio) {
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;
  const uint16_t width = seg.is_2d() ? seg.width() : seg_len;
  const uint16_t height = seg.height();

  if (seg.call == 0) {
    seg.fill(seg.color(1));
    seg.aux0 = UINT16_MAX;
    seg.aux1 = UINT16_MAX;
  }
  seg.fade_out(128);

  bool valid1 = (seg.aux0 < width * height);
  bool valid2 = (seg.aux1 < width * height);
  uint32_t sv1 = 0, sv2 = 0;

  // WLEDMM begin. Always answerable here, see the note on Popcorn audio.
  AudioData &audio = seg.audio();
  bool addPixels = true;                              // false -> inhibit new pixels in silence
  unsigned myIntensity = 129 - (seg.intensity >> 1);  // make parameter explicit, so we can work with it
  int soundColor = -1;                                // -1 = random color; 0..255 = use as palette index

  if (useaudio) {
    float volumeSmth = audio.volume_smth;
    float FFT_MajorPeak = audio.fft_major_peak;
    uint8_t samplePeak = audio.sample_peak;
    if ((volumeSmth > 1.0f) && (FFT_MajorPeak > 60.0f)) {  // we have sound - select color based on major frequency
      float musicIndex = logf(FFT_MajorPeak);              // log scaling of peak freq
      soundColor = mapf(musicIndex, 4.6f, 9.06f, 0, 255);  // pick color from frequency (4.6 = ln(100), 9.06 = ln(8600))
      soundColor = constrain(soundColor, 0, 255);          // remove over-shoot
      if (samplePeak > 0)
        myIntensity -= myIntensity / 2;  // increase effect intensity at peaks
      else if (volumeSmth > 96.0f)
        myIntensity -= myIntensity / 4;  // increase effect intensity slightly when music plays
      myIntensity = constrain(myIntensity, 0u, 129u);
    } else {                     // silence -> fade away
      valid1 = valid2 = false;   // do not copy last pixels
      addPixels = false;         // don't add new pixels
    }
  }
  // WLEDMM end

  if (valid1)
    sv1 = seg.is_2d() ? seg.get_pixel_color_xy(seg.aux0 % width, seg.aux0 / width)
                      : seg.get_pixel_color(seg.aux0);  // get spark color
  if (valid2)
    sv2 = seg.is_2d() ? seg.get_pixel_color_xy(seg.aux1 % width, seg.aux1 / width) : seg.get_pixel_color(seg.aux1);
  if (!seg.step)
    seg.blur(16);
  if (valid1) {
    if (seg.is_2d())
      seg.set_pixel_color_xy(seg.aux0 % width, seg.aux0 / width, sv1);
    else
      seg.set_pixel_color(seg.aux0, sv1);
  }  // restore spark color after blur
  if (valid2) {
    if (seg.is_2d())
      seg.set_pixel_color_xy(seg.aux1 % width, seg.aux1 / width, sv2);
    else
      seg.set_pixel_color(seg.aux1, sv2);
  }  // restore old spark color after blur

  if (addPixels)  // WLEDMM
    for (int i = 0; i < std::max(1, width / 20); i++) {
      if (hw_random8(myIntensity) == 0) {  // WLEDMM
        uint16_t index = hw_random16(width * height);
        uint16_t j = index % width, k = index / width;
        uint32_t col = seg.color_from_palette((soundColor > 0) ? soundColor + hw_random8(24) : hw_random8(), false,
                                              false, 0);  // WLEDMM
        if (seg.is_2d())
          seg.set_pixel_color_xy(j, k, col);
        else
          seg.set_pixel_color(index, col);
        seg.aux1 = seg.aux0;  // old spark
        seg.aux0 = index;     // remember where spark occurred
      }
    }
}

void mode_fireworks_audio(Segment &seg) { mode_fireworks_core(seg, true); }
#endif

// ---------------------------------------------------------------------------
// GEQ 3D
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GEQ_3D
/* GEQ 3D carries two licence statements four lines apart in WLED-MM's FX.cpp, and
 * neither is dropped here. Both are quoted verbatim below. EUPL-1.2-or-later and
 * GPLv3 are each compatible with this repository's GPL-3.0-or-later, so the
 * contradiction does not block the port, but it is unresolved upstream and the
 * authors are the only people who can settle it.
 *
 * WLED-MM wled00/FX.cpp:9048-9054, the block above the function:
 *
 *   @title     MoonModules WLED - GEQ 3D Effect
 *   @file      included in FX.cpp
 *   @repo      https://github.com/MoonModules/WLED-MM, submit changes to this file as PRs to MoonModules/WLED-MM
 *   @Authors   https://github.com/MoonModules/WLED-MM/commits/mdev/
 *   @Copyright (c) 2024 Github MoonModules Commit Authors (contact moonmodules@icloud.com for details)
 *   @license   Licensed under the EUPL-1.2 or later
 *
 * WLED-MM wled00/FX.cpp:9062-9063, the first two lines inside the function:
 *
 *   // Author: @TroyHacks
 *   // @license GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 */

/////////////////////////
//     ** 3D GEQ       //
/////////////////////////
void mode_GEQLASER(Segment &seg) {
  // Author: @TroyHacks
  // @license GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up
  const int cols = seg.width();
  const int rows = seg.height();
  if ((cols < 3) || (rows < 3))
    FX_FALLBACK_STATIC;  // too small

  int16_t *projector = reinterpret_cast<int16_t *>(&(seg.aux0));      // *projector is an alias for aux0 (uint16_t)
  int16_t *projector_dir = reinterpret_cast<int16_t *>(&(seg.aux1));  // *projector_dir is an alias for aux1 (uint16_t)

  if (seg.call == 0) {
    *projector = 0;
    *projector_dir = 1;
    seg.fill(BLACK);
  } else {
    if (seg.call % wf_map(seg.speed, 0, 255, 10, 1) == 0)
      *projector += *projector_dir;
    if (*projector >= cols)
      *projector_dir = -1;
    if (*projector <= 0)
      *projector_dir = 1;
  }
  *projector = constrain(*projector, 0, cols - 1);  // make sure we don't walk out of range

  seg.fill(BLACK);

  uint32_t ledColorTemp;
  // custom3 is 0..31 - constrain NUM_BANDS between 2(for split) and cols (for small width segments)
  const int NUM_BANDS = std::max(2, std::min(cols, int(map2(seg.custom3, 0, 31, 1, NUM_GEQ_CHANNELS))));
  int split = map2(*projector, 0, seg.width(), 0, (NUM_BANDS - 1));
  int horizon = map2(seg.custom1, 0, 255, rows - 1, 0);
  uint8_t depth = seg.custom2;  // depth of perspective. 255 = infinite ("laser")

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  uint8_t heights[NUM_GEQ_CHANNELS] = {0};
  // slightly reduce bar height on small panels
  const uint8_t maxHeight = roundf(float(rows) * ((rows < 18) ? 0.75f : 0.85f));
  for (int i = 0; i < NUM_BANDS; i++) {
    unsigned band = i;
    if (NUM_BANDS < NUM_GEQ_CHANNELS)
      band = map2(band, 0, NUM_BANDS - 1, 0, NUM_GEQ_CHANNELS - 1);  // always use full range.
    // cache fftResult[] as data might be updated in parallel by the audioreactive core
    heights[i] = map8(fftResult[band], 0, maxHeight);
  }

  for (int i = 0; i <= split; i++) {  // paint right vertical faces and top - LEFT to RIGHT
    uint16_t colorIndex = wf_map(cols / NUM_BANDS * i, 0, cols - 1, 0, 255);
    uint32_t ledColor = seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0);
    int linex = i * (cols / NUM_BANDS);

    if (heights[i] > 1) {
      ledColorTemp = color_fade(ledColor, 32, true);
      int pPos = std::max(0, linex + (cols / NUM_BANDS) - 1);
      // don't bother drawing what we'll hide anyway
      for (int y = (i < NUM_BANDS - 1) ? heights[i + 1] : 0; y <= heights[i]; y++) {
        if (rows - y > 0)
          draw_line_depth(seg, pPos, rows - y - 1, *projector, horizon, ledColorTemp, false, depth);  // right side
      }

      ledColorTemp = color_fade(ledColor, 128, true);
      // draw if above horizon AND not directly under projector (special case later)
      if (heights[i] < rows - horizon && (*projector <= linex || *projector >= pPos)) {
        if (rows - heights[i] > 1) {  // sanity check - avoid negative Y
          for (int x = linex; x <= pPos; x++) {
            bool doSoft = seg.check2 && ((x == linex) || (x == pPos));  // only first and last line need AA
            draw_line_depth(seg, x, rows - heights[i] - 2, *projector, horizon, ledColorTemp, doSoft,
                            depth);  // top perspective
          }
        }
      }
    }
  }

  for (int i = (NUM_BANDS - 1); i > split; i--) {  // paint left vertical faces and top - RIGHT to LEFT
    uint16_t colorIndex = wf_map(cols / NUM_BANDS * i, 0, cols - 1, 0, 255);
    uint32_t ledColor = seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0);
    int linex = i * (cols / NUM_BANDS);
    int pPos = std::max(0, linex + (cols / NUM_BANDS) - 1);

    if (heights[i] > 1) {
      ledColorTemp = color_fade(ledColor, 32, true);
      // don't bother drawing what we'll hide anyway
      for (int y = (i > 0) ? heights[i - 1] : 0; y <= heights[i]; y++) {
        if (rows - y > 0)
          draw_line_depth(seg, linex, rows - y - 1, *projector, horizon, ledColorTemp, false, depth);  // left side
      }

      ledColorTemp = color_fade(ledColor, 128, true);
      // draw if above horizon AND not directly under projector (special case later)
      if (heights[i] < rows - horizon && (*projector <= linex || *projector >= pPos)) {
        if (rows - heights[i] > 1) {  // sanity check - avoid negative Y
          for (int x = linex; x <= pPos; x++) {
            bool doSoft = seg.check2 && ((x == linex) || (x == pPos));  // only first and last line need AA
            draw_line_depth(seg, x, rows - heights[i] - 2, *projector, horizon, ledColorTemp, doSoft,
                            depth);  // top perspective
          }
        }
      }
    }
  }

  for (int i = 0; i < NUM_BANDS; i++) {
    uint16_t colorIndex = wf_map(cols / NUM_BANDS * i, 0, cols - 1, 0, 255);
    uint32_t ledColor = seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0);
    int linex = i * (cols / NUM_BANDS);
    int pPos = linex + (cols / NUM_BANDS) - 1;
    int pPos1 = linex + (cols / NUM_BANDS);

    if (*projector >= linex && *projector <= pPos) {  // special case when top perspective is directly under the projector
      if ((heights[i] > 1) && (heights[i] < rows - horizon) && (rows - heights[i] > 1)) {
        ledColorTemp = color_fade(ledColor, 128, true);
        for (int x = linex; x <= pPos; x++) {
          bool doSoft = seg.check2 && ((x == linex) || (x == pPos));  // only first and last line need AA
          draw_line_depth(seg, x, rows - heights[i] - 2, *projector, horizon, ledColorTemp, doSoft,
                          depth);  // top perspective
        }
      }
    }

    if ((heights[i] > 1) && (rows - heights[i] > 0)) {
      ledColorTemp = color_fade(ledColor, seg.intensity, true);
      for (int x = linex; x < pPos1; x++) {
        seg.draw_line(x, rows - 1, x, rows - heights[i] - 1, ledColorTemp);  // front fill
      }

      if (!seg.check1 && heights[i] > rows - horizon) {
        if (seg.intensity == 0)
          ledColorTemp = color_fade(ledColor, 32, true);  // match side fill if we're in blackout mode
        seg.draw_line(linex, rows - heights[i] - 1, linex + (cols / NUM_BANDS) - 1, rows - heights[i] - 1,
                      ledColorTemp);  // top line to simulate hidden top fill
      }

      if ((seg.check1) && (rows - heights[i] > 1)) {
        seg.draw_line(linex, rows - 1, linex, rows - heights[i] - 1, ledColor);  // left side line
        seg.draw_line(linex + (cols / NUM_BANDS) - 1, rows - 1, linex + (cols / NUM_BANDS) - 1, rows - heights[i] - 1,
                      ledColor);  // right side line
        seg.draw_line(linex, rows - heights[i] - 2, linex + (cols / NUM_BANDS) - 1, rows - heights[i] - 2,
                      ledColor);  // top line
        seg.draw_line(linex, rows - 1, linex + (cols / NUM_BANDS) - 1, rows - 1, ledColor);  // bottom line
      }
    }
  }
}
#endif

// ---------------------------------------------------------------------------
// Paintbrush
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PAINTBRUSH
/* Paintbrush is unambiguously GPLv3 in WLED-MM. The grant block at
 * WLED-MM wled00/FX.cpp:9195-9211 is quoted verbatim:
 *
 *   @title     MoonModules WLED - Painbrush Effect
 *   @file      included in FX.cpp
 *   @repo      https://github.com/MoonModules/WLED-MM, submit changes to this file as PRs to MoonModules/WLED-MM
 *   @Authors   https://github.com/MoonModules/WLED-MM/commits/mdev/
 *   @Copyright (c) 2024 Github MoonModules Commit Authors (contact moonmodules@icloud.com for details)
 *   @license   GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 *
 *     This function is part of the MoonModules WLED fork also known as "WLED-MM".
 *     WLED-MM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
 *     as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 *
 *     WLED-MM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License along with WLED-MM. If not, see <https://www.gnu.org/licenses/>.
 */

///////////////////////
//   2D Paintbrush   //
///////////////////////
void mode_2DPaintbrush(Segment &seg) {
  // Author: @TroyHacks
  // @license GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const uint16_t cols = seg.width();
  const uint16_t rows = seg.height();

  if (!seg.allocate_data(4))
    FX_FALLBACK_STATIC;  // allocation failed

  if (seg.call == 0) {
    seg.fill(BLACK);
    seg.aux0 = 0;
  }

  bool phase_chaos = seg.check3;
  bool soft = seg.check2;
  bool color_chaos = seg.check1;
  CRGB color;

  uint8_t numLines = map8(seg.intensity, 1, 16);

  seg.aux0++;  // hue
  seg.fade_to_black_by(map8(seg.custom1, 10, 128));

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result;

  seg.aux1 = phase_chaos ? hw_random8() : 0;

  for (size_t i = 0; i < numLines; i++) {
    uint8_t bin = wf_map(i, 0, numLines, 0, 15);

    uint8_t x1 = beatsin8_t(std::max(16, int(seg.speed)) / 16 * 1 + fftResult[0] / 16, 0, (cols - 1), seg.now,
                            fftResult[bin], seg.aux1);
    uint8_t x2 = beatsin8_t(std::max(16, int(seg.speed)) / 16 * 2 + fftResult[0] / 16, 0, (cols - 1), seg.now,
                            fftResult[bin], seg.aux1);
    uint8_t y1 = beatsin8_t(std::max(16, int(seg.speed)) / 16 * 3 + fftResult[0] / 16, 0, (rows - 1), seg.now,
                            fftResult[bin], seg.aux1);
    uint8_t y2 = beatsin8_t(std::max(16, int(seg.speed)) / 16 * 4 + fftResult[0] / 16, 0, (rows - 1), seg.now,
                            fftResult[bin], seg.aux1);

    int length = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    length = map8(fftResult[bin], 0, length);

    if (length > std::max(1, int(seg.custom3))) {
      if (color_chaos) {
        color = ColorFromPalette(seg.palette_ref(), i * 255 / numLines + (seg.aux0 & 0xFF), 255, LINEARBLEND);
      } else {
        uint16_t colorIndex = wf_map(i, 0, numLines, 0, 255);
        color = seg.color_from_palette(colorIndex, false, seg.palette_solid_wrap(), 0);
      }
      draw_line_depth(seg, x1, y1, x2, y2, color, soft, length);
    }
  }
}  // mode_2DPaintbrush()
#endif

// ---------------------------------------------------------------------------
// Snow Fall
// ---------------------------------------------------------------------------

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SNOW_FALL
// WLED-MM FX.cpp:5824. The bit array accessors Snow Fall tracks its flakes with.
bool getBitValue(const uint8_t *byteArray, size_t n) {
  size_t byteIndex = n / 8;
  size_t bitIndex = n % 8;
  uint8_t byteValue = byteArray[byteIndex];
  return (byteValue >> bitIndex) & 1;
}

void setBitValue(uint8_t *byteArray, size_t n, bool value) {
  size_t byteIndex = n / 8;
  size_t bitIndex = n % 8;
  if (value)
    byteArray[byteIndex] |= (1 << bitIndex);
  else
    byteArray[byteIndex] &= ~(1 << bitIndex);
}

/* MM reaches the flake grid through the global XY() macro, which is
 * Segment::XY(): the wrapped pixel index of (x, y) on the segment. There is one
 * canvas and one segment here and Snow Fall is 2D only, so it is the same
 * arithmetic. */
unsigned snowfall_xy(const Segment &seg, unsigned x, unsigned y) {
  const unsigned cols = seg.width();
  const unsigned rows = seg.height();
  return (x % cols) + (y % rows) * cols;
}

/* MM shuffles the column order with std::random_shuffle, which C++14 deprecated
 * and C++17 removed. This is the same algorithm (the Fisher-Yates walk libstdc++
 * used for it) against the engine's own random source, so the simulator stays
 * reproducible. */
void snowfall_shuffle(uint16_t *first, size_t count) {
  for (size_t i = count; i > 1; i--)
    std::swap(first[i - 1], first[hw_random16(i)]);
}

void mode_2DSnowFall(Segment &seg) {  // By: Brandon Butler
  // Uses bit array to track snow/particles
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // Not a 2D set-up
  const uint16_t cols = seg.width();
  const uint16_t rows = seg.height();
  const size_t dataSize = (seg.length() + 7) / 8;  // Round up to nearest byte

  /* MM puts its shuffled column order on the stack as `uint16_t
   * shuffledIndices[cols]`, a variable length array, which this port does not
   * allow. The scratch is taken from the same allocation instead, right after the
   * bit grid; allocate_data() is a no-op once the size settles, so this is still
   * an allocation on effect start only. */
  const size_t scratchSize = size_t(cols) * sizeof(uint16_t);
  if (!seg.allocate_data(dataSize + scratchSize))
    FX_FALLBACK_STATIC;  // Allocation failed
  uint8_t *grid = reinterpret_cast<uint8_t *>(seg.data);
  uint16_t *shuffledIndices = reinterpret_cast<uint16_t *>(seg.data + dataSize);

  bool overlay = seg.check2;  // Overlay is inverted. Only draws non-snow. Layer 1 controls snow color
  uint32_t bgColor = seg.color(1);

  if (seg.call == 0) {
    seg.fill(bgColor);
    seg.aux0 = 0;  // Overflow value
    memset(grid, 0, dataSize);
  }

  // Draw non snow for inverted overlay
  if (overlay) {
    for (int x = 0; x < cols; x++)
      for (int y = 0; y < rows; y++) {
        if (!getBitValue(grid, y * cols + x))
          seg.set_pixel_color_xy(x, y, bgColor);
      }
  }

  // fix SEGENV.step in case that timebase jumps
  if (std::abs(long(seg.now) - long(seg.step)) > 2000)
    seg.step = 0;

  uint8_t speed = wf_map(seg.speed, 0, 255, 0, 60);  // Updates per second
  if (!speed || seg.now - seg.step < 1000u / speed)
    return;  // Not enough time passed

  uint8_t blur = wf_map(seg.custom2, 0, 255, 255, 0);
  uint8_t sway = seg.custom3;

  // Despawn snow
  bool overflow = seg.aux0 && seg.check3;
  // 255 goes to 256, allows always despawn
  int despawnChance = seg.custom1 == 255 ? 256 : wf_map(seg.custom1, 0, 255, 0, 100);
  int lastY = rows - 1;
  for (int x = 0; x < cols; x++) {
    if (overflow || hw_random8() < despawnChance)
      setBitValue(grid, lastY * cols + x, 0);
    if (overlay || getBitValue(grid, lastY * cols + x))
      continue;  // Skip drawing if inverted overlay or snow
    seg.blend_pixel_color_xy(x, lastY, bgColor, blur);
  }
  if (seg.aux0)
    --seg.aux0;  // Decrease overflow

  // Precompute shuffled indices, helps randomize snow movement
  for (int i = 0; i < cols; i++)
    shuffledIndices[i] = i;
  snowfall_shuffle(shuffledIndices, cols);

  // Update snow, loop from 2nd bottom row to top with precomputed random order
  for (int y = rows - 2; y >= 0; y--) {
    for (int i = 0; i < cols; i++) {
      int x = shuffledIndices[i];

      int pos = snowfall_xy(seg, x, y);

      uint32_t xyColor = seg.get_pixel_color_xy(x, y);  // Limit getPixelColorXY calls
      if (!getBitValue(grid, pos)) {                    // No snow, fade if needed and skip
        if (!overlay && blur)
          seg.set_pixel_color_xy(x, y, color_blend(xyColor, bgColor, blur));
        continue;
      }

      int newX = x, newY = y + 1;
      int newPos = snowfall_xy(seg, newX, newY);
      // Open Position Booleans
      bool down = !getBitValue(grid, newPos);
      bool downLeft = x > 0 && !getBitValue(grid, newPos - 1);
      bool downRight = x < cols - 1 && !getBitValue(grid, newPos + 1);

      if (!down) {
        if (downLeft && downRight)
          newX = hw_random8(2) ? x - 1 : x + 1;
        else if (downLeft)
          newX = x - 1;
        else if (downRight)
          newX = x + 1;
        else
          newY = y;  // Snow is stuck
      } else if (sway && hw_random8(30) < sway) {  // Sway falling snow if horizontal and diagonal directions are open
        if (x % 2 == 1 && downLeft && !getBitValue(grid, pos - 1))
          newX = x - 1;  // Odd  Columns Move Left
        else if (x % 2 == 0 && downRight && !getBitValue(grid, pos + 1))
          newX = x + 1;  // Even Columns Move Right
      }

      if (newY != y || newX != x) {                         // Snow moved
        setBitValue(grid, pos, 0);                          // Clear old
        setBitValue(grid, snowfall_xy(seg, newX, newY), 1);  // Set new
        if (!overlay)
          seg.set_pixel_color_xy(x, y, color_blend(xyColor, bgColor, blur));  // Fade old
      }
      if (!overlay)
        seg.set_pixel_color_xy(newX, newY, xyColor);  // Draw new / redraw stuck
    }
  }

  // Spawn snow
  int spawnChance = wf_map(seg.intensity, 0, 255, 0, 100);
  for (int x = 0; x < cols; x++) {  // y = 0
    if (hw_random8() >= spawnChance)
      continue;
    if (getBitValue(grid, x)) {
      seg.aux0 = rows;
      continue;
    }  // Snow exists, overflowing

    setBitValue(grid, x, 1);  // Spawn snow
    if (overlay)
      continue;  // Skip drawing if inverted overlay

    if (seg.check1)
      seg.set_pixel_color_xy(x, 0, ColorFromPalette(seg.palette_ref(), hw_random8()));  // Use palette
    else {
      int c = hw_random8(120, 200);
      seg.set_pixel_color_xy(x, 0, c, c, c);
    }  // Use snow color
  }

  seg.step = seg.now;
}  // mode_2DSnowFall()
#endif

/* Registration table. Names are MM's display names with the trailing moon glyph
 * removed, exactly as BATCHES.md specifies. */
const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METEOR_SMOOTH
    {"Meteor Smooth@!,Trail,,,,Gradient;;!;1", mode_meteor_smooth},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PARTY_JERK
    {"Party jerk@Effect speed,Sensitivity,Color change speed,Effect speed active multiplier;!,!;!;1v;c1=8,c2=48,m12=0,"
     "si=0",
     mode_partyjerk},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POPCORN_AUDIO
    {"Popcorn audio@!,!,,,,,Overlay;!,!,!;!;1v,1.5d;m12=1", mode_popcorn_audio},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MULTI_COMET_AUDIO
    // MM's string ends "m12=7", its Pinwheel. Pinwheel is 4 in this engine's
    // mapping1D2D numbering, which is upstream's; see recon/wled-mm-inventory.md
    // section 6b. Only the number changed, the mapping MM asks for is the same.
    {"Multi Comet audio@Speed,Tail Length;!,!;!;1v;sx=160,ix=32,m12=4,si=1", mode_multi_comet_ar},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FW_STARBURST_AUDIO
    {"Fw Starburst audio@Chance,Fragments,,,,,Overlay;,!;!;1v;pal=11,m12=0", mode_starburst_audio},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_AUDIO
    {"Fireworks audio@,Frequency;!,!;!;1v,12;ix=192,pal=11", mode_fireworks_audio},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GEQ_3D
    {"GEQ 3D@Speed,Front Fill,Horizon,Depth,Num Bands,Borders,Soft,;!,,Peaks;!;2f;sx=255,ix=228,c1=255,c2=255,c3=15,"
     "pal=11",
     mode_GEQLASER},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PAINTBRUSH
    {"Paintbrush@Oscillator Offset,# of lines,Fade Rate,,Min Length,Color Chaos,Anti-aliasing,Phase Chaos;!,,Peaks;!;"
     "2f;sx=160,ix=255,c1=80,c2=255,c3=0,pal=72,o1=0,o2=1,o3=0",
     mode_2DPaintbrush},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SNOW_FALL
    {"Snow Fall@!,Spawn Rate,Despawn Rate,Blur,Sway Chance,Use Palette,Inverted Overlay,Prevent Overflow,;!,!;!;2;"
     "sx=128,ix=16,c1=17,c2=0,c3=0,o1=0,o2=0,o3=1",
     mode_2DSnowFall},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_MM;
const EffectGroup EFFECT_GROUP_MM{"mm", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_MM
