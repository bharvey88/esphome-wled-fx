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

/* Effects that render natively in both 1D and 2D. See BATCHES.md. */

#include <algorithm>

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_1D2D                                                                        \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS || WLED_FX_FX_RAIN || WLED_FX_FX_RIPPLE ||      \
   WLED_FX_FX_RIPPLE_RAINBOW || WLED_FX_FX_PALETTE || WLED_FX_FX_HALLOWEEN_EYES ||                \
   WLED_FX_FX_FIREWORKS_1D)

#if WLED_FX_GROUP_1D2D

// Arduino's math.h hands WLED these two. Neither is guaranteed by <cmath>, so the
// file defines them when the toolchain has not, and Palette keeps its upstream
// expressions unchanged.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TWOPI
#define M_TWOPI (M_PI * 2.0)
#endif

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS || WLED_FX_FX_RAIN
/*
 * Fireworks function.
 */
void mode_fireworks(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const uint16_t width = seg.is_2d() ? seg.width() : seg_len;
  const uint16_t height = seg.height();

  if (seg.call == 0) {
    seg.aux0 = UINT16_MAX;
    seg.aux1 = UINT16_MAX;
  }
  seg.fade_out(128);

  uint8_t x = seg.aux0 % width, y = seg.aux0 / width;  // 2D coordinates stored in upper and lower byte
  if (!seg.step) {
    // fireworks mode (blur flares)
    bool valid1 = (seg.aux0 < width * height);
    bool valid2 = (seg.aux1 < width * height);
    uint32_t sv1 = 0, sv2 = 0;
    if (valid1)
      sv1 = seg.is_2d() ? seg.get_pixel_color_xy(x, y) : seg.get_pixel_color(seg.aux0);  // get spark color
    if (valid2)
      sv2 = seg.is_2d() ? seg.get_pixel_color_xy(x, y) : seg.get_pixel_color(seg.aux1);
    seg.blur(16);  // used in mode_rain()
    if (valid1) {
      if (seg.is_2d())
        seg.set_pixel_color_xy(x, y, sv1);
      else
        seg.set_pixel_color(seg.aux0, sv1);
    }  // restore spark color after blur
    if (valid2) {
      if (seg.is_2d())
        seg.set_pixel_color_xy(x, y, sv2);
      else
        seg.set_pixel_color(seg.aux1, sv2);
    }  // restore old spark color after blur
  }

  for (int i = 0; i < std::max(1, width / 20); i++) {
    if (hw_random8(129 - (seg.intensity >> 1)) == 0) {
      uint16_t index = hw_random16(width * height);
      x = index % width;
      y = index / width;
      uint32_t col = seg.color_from_palette(hw_random8(), false, false, 0);
      if (seg.is_2d())
        seg.set_pixel_color_xy(x, y, col);
      else
        seg.set_pixel_color(index, col);
      seg.aux1 = seg.aux0;  // old spark
      seg.aux0 = index;     // remember where spark occurred
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAIN
// WLED FX.h:113 spells this as a macro over SEGMENT and SEGLEN. Here it reads the
// `seg` and `seg_len` of the effect that uses it, so the call site stays verbatim.
#define SPEED_FORMULA_L (5U + (50U * (255U - seg.speed)) / seg_len)

// Twinkling LEDs running. Inspired by https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/Rain.h
void mode_rain(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const unsigned width = seg.width();
  const unsigned height = seg.height();
  seg.step += FRAMETIME;
  if (seg.call && seg.step > SPEED_FORMULA_L) {
    seg.step = 1;
    if (seg.is_2d()) {
      // uint32_t ctemp[width];
      // for (int i = 0; i<width; i++) ctemp[i] = seg.get_pixel_color_xy(i, height-1);
      seg.move(6, 1, true);  // move all pixels down
      // for (int i = 0; i<width; i++) seg.set_pixel_color_xy(i, 0, ctemp[i]); // wrap around
      seg.aux0 = (seg.aux0 % width) + (seg.aux0 / width + 1) * width;
      seg.aux1 = (seg.aux1 % width) + (seg.aux1 / width + 1) * width;
    } else {
      // shift all leds left
      uint32_t ctemp = seg.get_pixel_color(0);
      for (unsigned i = 0; i < seg_len - 1; i++) {
        seg.set_pixel_color(i, seg.get_pixel_color(i + 1));
      }
      seg.set_pixel_color(seg_len - 1, ctemp);  // wrap around
      seg.aux0++;                               // increase spark index
      seg.aux1++;
    }
    if (seg.aux0 == 0)
      seg.aux0 = UINT16_MAX;  // reset previous spark position
    if (seg.aux1 == 0)
      seg.aux0 = UINT16_MAX;  // reset previous spark position
    if (seg.aux0 >= width * height)
      seg.aux0 = 0;  // ignore
    if (seg.aux1 >= width * height)
      seg.aux1 = 0;
  }
  mode_fireworks(seg);
}
#undef SPEED_FORMULA_L
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE || WLED_FX_FX_RIPPLE_RAINBOW
// Water ripple
// propagation velocity from speed
// drop rate from intensity

// 4 bytes
typedef struct Ripple {
  uint8_t state;
  uint8_t color;
  uint16_t pos;
} ripple;

// WLED FX.cpp:69, the base chance denominator the drop rate is drawn against.
constexpr int IBN = 5100;

constexpr int MAX_RIPPLES = 100;
void ripple_base(Segment &seg, uint8_t blurAmount = 0) {
  const unsigned seg_len = seg.length();
  unsigned maxRipples = std::min(1 + (int) (seg_len >> 2), MAX_RIPPLES);  // 56 max for 16 segment ESP8266
  unsigned dataSize = sizeof(ripple) * maxRipples;

  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  Ripple *ripples = reinterpret_cast<Ripple *>(seg.data);

  // draw wave
  for (unsigned i = 0; i < maxRipples; i++) {
    unsigned ripplestate = ripples[i].state;
    if (ripplestate) {
      unsigned rippledecay = (seg.speed >> 4) + 1;  // faster decay if faster propagation
      unsigned rippleorigin = ripples[i].pos;
      uint32_t col = seg.color_from_palette(ripples[i].color, false, false, 255);
      unsigned propagation = ((ripplestate / rippledecay - 1) * (seg.speed + 1));
      int propI = propagation >> 8;
      unsigned propF = propagation & 0xFF;
      unsigned amp = (ripplestate < 17) ? triwave8((ripplestate - 1) * 8) : wf_map(ripplestate, 17, 255, 255, 2);

      if (seg.is_2d()) {
        propI /= 2;
        unsigned cx = rippleorigin >> 8;
        unsigned cy = rippleorigin & 0xFF;
        unsigned mag = scale8(sin8_t((propF >> 2)), amp);
        if (propI > 0)
          seg.draw_circle(cx, cy, propI, color_blend(seg.get_pixel_color_xy(cx + propI, cy), col, mag), true);
      } else {
        int left = rippleorigin - propI - 1;
        int right = rippleorigin + propI + 2;
        for (int v = 0; v < 4; v++) {
          uint8_t mag = scale8(cubicwave8((propF >> 2) + v * 64), amp);
          seg.set_pixel_color(left + v, color_blend(seg.get_pixel_color(left + v), col, mag));     // TODO
          seg.set_pixel_color(right - v, color_blend(seg.get_pixel_color(right - v), col, mag));   // TODO
        }
      }
      ripplestate += rippledecay;
      ripples[i].state = (ripplestate > 254) ? 0 : ripplestate;
    } else {  // randomly create new wave
      if (hw_random16(IBN + 10000) <= (seg.intensity >> (seg.is_2d() * 3))) {
        ripples[i].state = 1;
        ripples[i].pos =
            seg.is_2d() ? ((hw_random8(seg.width()) << 8) | (hw_random8(seg.height()))) : hw_random16(seg_len);
        ripples[i].color = hw_random8();  // color
      }
    }
  }
  seg.blur(blurAmount);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE
void mode_ripple(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  if (seg.custom1 || seg.check2)  // blur or overlay
    seg.fade_out(250);
  else
    seg.fill(seg.color(1));

  ripple_base(seg, seg.custom1 >> 1);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE_RAINBOW
void mode_ripple_rainbow(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  if (seg.call == 0) {
    seg.aux0 = hw_random8();
    seg.aux1 = hw_random8();
  }
  if (seg.aux0 == seg.aux1) {
    seg.aux1 = hw_random8();
  } else if (seg.aux1 > seg.aux0) {
    seg.aux0++;
  } else {
    seg.aux0--;
  }
  seg.fill(color_blend(seg.color_wheel(seg.aux0), BLACK, uint8_t(235)));
  ripple_base(seg);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PALETTE
void mode_palette(Segment &seg) {
  // Set up some compile time constants so that we can handle integer and float based modes using the same code base.
#ifdef ESP8266
  using mathType = int32_t;
  using wideMathType = int64_t;
  using angleType = unsigned;
  constexpr mathType sInt16Scale = 0x7FFF;
  constexpr mathType maxAngle = 0x8000;
  constexpr mathType staticRotationScale = 256;
  constexpr mathType animatedRotationScale = 1;
  constexpr int16_t (*sinFunction)(uint16_t) = &sin16_t;
  constexpr int16_t (*cosFunction)(uint16_t) = &cos16_t;
#else
  using mathType = float;
  using wideMathType = float;
  using angleType = float;
  constexpr mathType sInt16Scale = 1.0f;
  constexpr mathType maxAngle = M_PI / 256.0;
  constexpr mathType staticRotationScale = 1.0f;
  constexpr mathType animatedRotationScale = M_TWOPI / double(0xFFFF);
  constexpr float (*sinFunction)(float) = &sin_t;
  constexpr float (*cosFunction)(float) = &cos_t;
#endif
  const bool isMatrix = seg.is_2d();
  const int cols = seg.width();
  const int rows = isMatrix ? seg.height() : 1;  // strip.getActiveSegmentsNum(): one canvas, one segment

  const int inputShift = seg.speed;
  const int inputSize = seg.intensity;
  const int inputRotation = seg.custom1;
  const bool inputAnimateShift = seg.check1;
  const bool inputAnimateRotation = seg.check2;
  const bool inputAssumeSquare = seg.check3;

  const angleType theta = (!inputAnimateRotation) ? ((inputRotation + 128) * maxAngle / staticRotationScale)
                                                  : (((seg.now * ((inputRotation >> 4) + 1)) & 0xFFFF) *
                                                     animatedRotationScale);
  const mathType sinTheta = sinFunction(theta);
  const mathType cosTheta = cosFunction(theta);

  const mathType maxX = std::max(1, cols - 1);
  const mathType maxY = std::max(1, rows - 1);
  // Set up some parameters according to inputAssumeSquare, so that we can handle anamorphic mode using the same code
  // base.
  const mathType maxXIn = inputAssumeSquare ? maxX : mathType(1);
  const mathType maxYIn = inputAssumeSquare ? maxY : mathType(1);
  const mathType maxXOut = !inputAssumeSquare ? maxX : mathType(1);
  const mathType maxYOut = !inputAssumeSquare ? maxY : mathType(1);
  const mathType centerX = sInt16Scale * maxXOut / mathType(2);
  const mathType centerY = sInt16Scale * maxYOut / mathType(2);
  // The basic idea for this effect is to rotate a rectangle that is filled with the palette along one axis, then map
  // our display to it, to find what color a pixel should have.
  // However, we want a) no areas of solid color (in front of or behind the palette), and b) we want to make use of the
  // full palette.
  // So the rectangle needs to have exactly the right size. That size depends on the rotation.
  // This scale computation here only considers one dimension. You can think of it like the rectangle is always scaled
  // so that the left and right most points always match the left and right side of the display.
  const mathType scale = std::abs(sinTheta) + (std::abs(cosTheta) * maxYOut / maxXOut);
  // 2D simulation:
  // If we are dealing with a 1D setup, we assume that each segment represents one line on a 2-dimensional display.
  // The function is called once per segments, so we need to handle one line at a time.
  const int yFrom = 0;  // strip.getCurrSegmentId(): one canvas, one segment
  const int yTo = isMatrix ? maxY : yFrom;
  for (int y = yFrom; y <= yTo; ++y) {
    // translate, scale, rotate
    const mathType ytCosTheta =
        mathType((wideMathType(cosTheta) * wideMathType(y * sInt16Scale - centerY * maxYIn)) /
                 wideMathType(maxYIn * scale));
    for (int x = 0; x < cols; ++x) {
      // translate, scale, rotate
      const mathType xtSinTheta =
          mathType((wideMathType(sinTheta) * wideMathType(x * sInt16Scale - centerX * maxXIn)) /
                   wideMathType(maxXIn * scale));
      // Map the pixel coordinate to an imaginary-rectangle-coordinate.
      // The y coordinate doesn't actually matter, as our imaginary rectangle is filled with the palette from left to
      // right, so all points at a given x-coordinate have the same color.
      const mathType sourceX = xtSinTheta + ytCosTheta + centerX;
      // The computation was scaled just right so that the result should always be in range [0, maxXOut], but enforce
      // this anyway to account for imprecision. Then scale it so that the range is [0, 255], which we can use with the
      // palette.
      int colorIndex = (std::min(std::max(sourceX, mathType(0)), maxXOut * sInt16Scale) * wideMathType(255)) /
                       (sInt16Scale * maxXOut);
      // inputSize determines by how much we want to scale the palette:
      // values < 128 display a fraction of a palette,
      // values > 128 display multiple palettes.
      if (inputSize <= 128) {
        colorIndex = (colorIndex * inputSize) / 128;
      } else {
        // Linear function that maps colorIndex 128=>1, 256=>9.
        // With this function every full palette repetition is exactly 16 configuration steps wide.
        // That allows displaying exactly 2 repetitions for example.
        colorIndex = ((inputSize - 112) * colorIndex) / 16;
      }
      // Finally, shift the palette a bit.
      const int paletteOffset =
          (!inputAnimateShift) ? (inputShift) : (((seg.now * ((inputShift >> 3) + 1)) & 0xFFFF) >> 8);
      colorIndex -= paletteOffset;
      const uint32_t color = seg.color_wheel((uint8_t) colorIndex);
      if (isMatrix) {
        seg.set_pixel_color_xy(x, y, color);
      } else {
        seg.set_pixel_color(x, color);
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HALLOWEEN_EYES
void mode_halloween_eyes(Segment &seg) {
  enum eyeState : uint8_t {
    initializeOn = 0,
    on,
    blink,
    initializeOff,
    off,

    count
  };
  struct EyeData {
    eyeState state;
    uint8_t color;
    uint16_t startPos;
    // duration + endTime could theoretically be replaced by a single endTime, however we would lose
    // the ability to end the animation early when the user reduces the animation time.
    uint16_t duration;
    uint32_t startTime;
    uint32_t blinkEndTime;
    // Upstream parks the matrix row in SEGMENT.offset, a segment field this engine
    // does not have. It lives with the rest of the effect state instead.
    uint16_t row;
  };

  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const unsigned maxWidth = seg.is_2d() ? seg.width() : seg_len;
  const unsigned HALLOWEEN_EYE_SPACE = std::max(2u, unsigned(seg.is_2d() ? seg.width() >> 4 : seg_len >> 5));
  const unsigned HALLOWEEN_EYE_WIDTH = HALLOWEEN_EYE_SPACE / 2;
  unsigned eyeLength = (2 * HALLOWEEN_EYE_WIDTH) + HALLOWEEN_EYE_SPACE;
  if (eyeLength >= maxWidth)
    FX_FALLBACK_STATIC;  // bail if segment too short

  if (!seg.allocate_data(sizeof(EyeData)))
    FX_FALLBACK_STATIC;  // allocation failed
  EyeData &data = *reinterpret_cast<EyeData *>(seg.data);

  if (!seg.check2)
    seg.fill(seg.color(1));  // fill background

  data.state = static_cast<eyeState>(data.state % eyeState::count);
  unsigned duration = std::max(uint16_t{1u}, data.duration);
  const uint32_t elapsedTime = seg.now - data.startTime;

  switch (data.state) {
    case eyeState::initializeOn: {
      // initialize the eyes-on state:
      // - select eye position and color
      // - select a duration
      // - immediately switch to eyes on state.

      data.startPos = hw_random16(0, maxWidth - eyeLength - 1);
      data.color = hw_random8();
      if (seg.is_2d())
        data.row = hw_random16(seg.height() - 1);
      duration = 128u + hw_random16(seg.intensity * 64u);
      data.duration = duration;
      data.state = eyeState::on;
      [[fallthrough]];
    }
    case eyeState::on: {
      // eyes-on steate:
      // - fade eyes in for some time
      // - keep eyes on until the pre-selected duration is over
      // - randomly switch to the blink (sub-)state, and initialize it with a blink duration (more precisely, a blink
      //   end time stamp)
      // - never switch to the blink state if the animation just started or is about to end

      unsigned start2ndEye = data.startPos + HALLOWEEN_EYE_WIDTH + HALLOWEEN_EYE_SPACE;
      // If the user reduces the input while in this state, limit the duration.
      duration = std::min(duration, (128u + (seg.intensity * 64u)));

      constexpr uint32_t minimumOnTimeBegin = 1024u;
      constexpr uint32_t minimumOnTimeEnd = 1024u;
      const uint32_t fadeInAnimationState = elapsedTime * uint32_t{256u * 8u} / duration;
      const uint32_t backgroundColor = seg.color(1);
      const uint32_t eyeColor = seg.color_from_palette(data.color, false, false, 0);
      uint32_t c = eyeColor;
      if (fadeInAnimationState < 256u) {
        c = color_blend(backgroundColor, eyeColor, uint8_t(fadeInAnimationState));
      } else if (elapsedTime > minimumOnTimeBegin) {
        const uint32_t remainingTime = (elapsedTime >= duration) ? 0u : (duration - elapsedTime);
        if (remainingTime > minimumOnTimeEnd) {
          if (hw_random8() < 4u) {
            c = backgroundColor;
            data.state = eyeState::blink;
            data.blinkEndTime = seg.now + hw_random8(8, 128);
          }
        }
      }

      if (c != backgroundColor) {
        // render eyes
        for (unsigned i = 0; i < HALLOWEEN_EYE_WIDTH; i++) {
          if (seg.is_2d()) {
            seg.set_pixel_color_xy(data.startPos + i, (unsigned) data.row, c);
            seg.set_pixel_color_xy(start2ndEye + i, (unsigned) data.row, c);
          } else {
            seg.set_pixel_color(data.startPos + i, c);
            seg.set_pixel_color(start2ndEye + i, c);
          }
        }
      }
      break;
    }
    case eyeState::blink: {
      // eyes-on but currently blinking state:
      // - wait until the blink time is over, then switch back to eyes-on

      if (seg.now >= data.blinkEndTime) {
        data.state = eyeState::on;
      }
      break;
    }
    case eyeState::initializeOff: {
      // initialize eyes-off state:
      // - select a duration
      // - immediately switch to eyes-off state

      const unsigned eyeOffTimeBase = seg.speed * 128u;
      duration = eyeOffTimeBase + hw_random16(eyeOffTimeBase);
      data.duration = duration;
      data.state = eyeState::off;
      [[fallthrough]];
    }
    case eyeState::off: {
      // eyes-off state:
      // - not much to do here

      // If the user reduces the input while in this state, limit the duration.
      const unsigned eyeOffTimeBase = seg.speed * 128u;
      duration = std::min(duration, (2u * eyeOffTimeBase));
      break;
    }
    case eyeState::count: {
      // Can't happen, not an actual state.
      data.state = eyeState::initializeOn;
      break;
    }
  }

  if (elapsedTime > duration) {
    // The current state duration is over, switch to the next state.
    switch (data.state) {
      case eyeState::initializeOn:
      case eyeState::on:
      case eyeState::blink:
        data.state = eyeState::initializeOff;
        break;
      case eyeState::initializeOff:
      case eyeState::off:
      case eyeState::count:
      default:
        data.state = eyeState::initializeOn;
        break;
    }
    data.startTime = seg.now;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_1D
// each needs 20 bytes
// Spark type is used for popcorn, 1D fireworks, and drip
typedef struct Spark {
  float pos, posX;
  float vel, velX;
  uint16_t col;
  uint8_t colIndex;
} spark;

/* WLED FX.h:101, FAIR_DATA_PER_SEG = MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS, the
 * share of the effect data budget one segment of many may claim. There is only
 * ever one segment here, so the two doublings upstream applies to a strip running
 * few segments are folded in: 64k / 32 * 4 is what WLED hands a lone segment on an
 * ESP32, and it keeps the spark count matching upstream on a large matrix. */
constexpr unsigned FAIR_DATA_PER_SEG = 8192;

/*
 * Exploding fireworks effect
 * adapted from: http://www.anirama.com/1000leds/1d-fireworks/
 * adapted for 2D WLED by blazoncek (Blaz Kristan (AKA blazoncek))
 */
void mode_exploding_fireworks(Segment &seg) {
  const unsigned seg_len = seg.length();
  if (seg_len <= 1)
    FX_FALLBACK_STATIC;
  const int cols = seg.is_2d() ? seg.width() : 1;
  const int rows = seg.is_2d() ? seg.height() : seg_len;

  // allocate segment data
  unsigned maxData = FAIR_DATA_PER_SEG;  // ESP8266: 256 ESP32: 640
  unsigned segs = 1;                     // strip.getActiveSegmentsNum()
  if (segs <= (1 / 2))
    maxData *= 2;  // ESP8266: 512 if <= 8 segs ESP32: 1280 if <= 16 segs
  if (segs <= (1 / 4))
    maxData *= 2;  // ESP8266: 1024 if <= 4 segs ESP32: 2560 if <= 8 segs
  int maxSparks = maxData / sizeof(spark);  // ESP8266: max. 21/42/85 sparks/seg, ESP32: max. 53/106/213 sparks/seg

  unsigned numSparks = std::min(5 + ((rows * cols) >> 1), maxSparks);
  unsigned dataSize = sizeof(spark) * numSparks;
  if (!seg.allocate_data(dataSize + sizeof(float)))
    FX_FALLBACK_STATIC;  // allocation failed
  float *dying_gravity = reinterpret_cast<float *>(seg.data + dataSize);

  if (dataSize != seg.aux1) {  // reset to flare if sparks were reallocated (it may be good idea to reset segment if
                               // bounds change)
    *dying_gravity = 0.0f;
    seg.aux0 = 0;
    seg.aux1 = dataSize;
  }

  seg.fade_out(252);

  Spark *sparks = reinterpret_cast<Spark *>(seg.data);
  Spark *flare = sparks;  // first spark is flare data

  float gravity = -0.0004f - (seg.speed / 800000.0f);  // m/s/s
  gravity *= rows;

  if (seg.aux0 < 2) {      // FLARE
    if (seg.aux0 == 0) {   // init flare
      flare->pos = 0;
      flare->posX =
          seg.is_2d() ? hw_random16(2, cols - 3) : (seg.intensity > hw_random8());  // will enable random firing side on 1D
      unsigned peakHeight = 75 + hw_random8(180);                                   // 0-255
      peakHeight = (peakHeight * (rows - 1)) >> 8;
      flare->vel = sqrtf(-2.0f * gravity * peakHeight);
      flare->velX = seg.is_2d() ? (hw_random8(9) - 4) / 64.0f : 0;  // no X velocity on 1D
      flare->col = 255;                                             // brightness
      seg.aux0 = 1;
    }

    // launch
    if (flare->vel > 12 * gravity) {
      // flare
      if (seg.is_2d())
        seg.set_pixel_color_xy(unsigned(flare->posX), rows - uint16_t(flare->pos) - 1, flare->col, flare->col,
                               flare->col);
      else
        seg.set_pixel_color((flare->posX > 0.0f) ? rows - int(flare->pos) - 1 : int(flare->pos), flare->col,
                            flare->col, flare->col);
      flare->pos += flare->vel;
      flare->pos = constrain(flare->pos, 0, rows - 1);
      if (seg.is_2d()) {
        flare->posX += flare->velX;
        flare->posX = constrain(flare->posX, 0, cols - 1);
      }
      flare->vel += gravity;
      flare->col -= 2;
    } else {
      seg.aux0 = 2;  // ready to explode
    }
  } else if (seg.aux0 < 4) {
    /*
     * Explode!
     *
     * Explosion happens where the flare ended.
     * Size is proportional to the height.
     */
    unsigned nSparks = flare->pos + hw_random8(4);
    nSparks = std::max(nSparks, 4U);  // This is not a standard constrain; numSparks is not guaranteed to be at least 4
    nSparks = std::min(nSparks, numSparks);

    // initialize sparks
    if (seg.aux0 == 2) {
      for (unsigned i = 1; i < nSparks; i++) {
        sparks[i].pos = flare->pos;
        sparks[i].posX = flare->posX;
        sparks[i].vel = (float(hw_random16(20001)) / 10000.0f) - 0.9f;  // from -0.9 to 1.1
        sparks[i].vel *= rows < 32 ? 0.5f : 1;                          // reduce velocity for smaller strips
        sparks[i].velX = seg.is_2d() ? (float(hw_random16(20001)) / 10000.0f) - 1.0f : 0;  // from -1 to 1
        sparks[i].col = 345;  // abs(sparks[i].vel * 750.0); // set colors before scaling velocity to keep them bright
        // sparks[i].col = constrain(sparks[i].col, 0, 345);
        sparks[i].colIndex = hw_random8();
        sparks[i].vel *= flare->pos / rows;                        // proportional to height
        sparks[i].velX *= seg.is_2d() ? flare->posX / cols : 0;    // proportional to width
        sparks[i].vel *= -gravity * 50;
      }
      // sparks[1].col = 345; // this will be our known spark
      *dying_gravity = gravity / 2;
      seg.aux0 = 3;
    }

    if (sparks[1].col > 4) {  //&& sparks[1].pos > 0) { // as long as our known spark is lit, work with all the sparks
      for (unsigned i = 1; i < nSparks; i++) {
        sparks[i].pos += sparks[i].vel;
        sparks[i].posX += sparks[i].velX;
        sparks[i].vel += *dying_gravity;
        sparks[i].velX += seg.is_2d() ? *dying_gravity : 0;
        if (sparks[i].col > 3)
          sparks[i].col -= 4;

        if (sparks[i].pos > 0 && sparks[i].pos < rows) {
          if (seg.is_2d() && !(sparks[i].posX >= 0 && sparks[i].posX < cols))
            continue;
          unsigned prog = sparks[i].col;
          uint32_t spColor = (seg.palette) ? seg.color_wheel(sparks[i].colIndex) : seg.color(0);
          CRGBW c = BLACK;      // HeatColor(sparks[i].col);
          if (prog > 300) {     // fade from white to spark color
            c = color_blend(spColor, WHITE, uint8_t((prog - 300) * 5));
          } else if (prog > 45) {  // fade from spark color to black
            c = color_blend(BLACK, spColor, uint8_t(prog - 45));
            unsigned cooling = (300 - prog) >> 5;
            c.g = qsub8(c.g, cooling);
            c.b = qsub8(c.b, cooling * 2);
          }
          if (seg.is_2d())
            seg.set_pixel_color_xy(int(sparks[i].posX), rows - int(sparks[i].pos) - 1, c);
          else
            seg.set_pixel_color(int(sparks[i].posX) ? rows - int(sparks[i].pos) - 1 : int(sparks[i].pos), c);
        }
      }
      if (seg.check3)
        seg.blur(16);
      *dying_gravity *= .8f;  // as sparks burn out they fall slower
    } else {
      seg.aux0 = 6 + hw_random8(10);  // wait for this many frames
    }
  } else {
    seg.aux0--;
    if (seg.aux0 < 4) {
      seg.aux0 = 0;  // back to flare
    }
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS
    {"Fireworks@,Frequency;!,!;!;12;ix=192,pal=11", mode_fireworks},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RAIN
    {"Rain@!,Spawning rate;!,!;!;12;ix=128,pal=0", mode_rain},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE
    {"Ripple@!,Wave #,Blur,,,,Overlay;,!;!;12;c1=0", mode_ripple},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_RIPPLE_RAINBOW
    {"Ripple Rainbow@!,Wave #;;!;12", mode_ripple_rainbow},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PALETTE
    {"Palette@Shift,Size,Rotation,,,Animate Shift,Animate Rotation,Anamorphic;;!;12;ix=112,c1=0,o1=1,o2=0,o3=1",
     mode_palette},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HALLOWEEN_EYES
    {"Halloween Eyes@Eye off time,Eye on time,,,,,Overlay;!,!;!;12", mode_halloween_eyes},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIREWORKS_1D
    {"Fireworks 1D@Gravity,Firing side;!,!;!;12;pal=11,ix=128", mode_exploding_fireworks},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_1D2D;
const EffectGroup EFFECT_GROUP_1D2D{"1d2d", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_1D2D
