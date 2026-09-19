#pragma once

/* Helpers that WLED 16.0.1 keeps as file-scope code in wled00/FX.cpp, wled00/FX.h
 * and wled00/util.cpp, and that more than one effect translation unit needs.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * Everything here is declared with external linkage on purpose. A `const` object
 * at namespace scope has internal linkage in C++, and an effect file that defines
 * its own copy of a shared helper cannot be diffed against upstream any more.
 * See PORTING.md section 6.
 */

#include <cstdint>

#include "wf_math.h"
#include "wf_segment.h"

namespace esphome {
namespace wled_fx {

// WLED FX.cpp:69. The base chance denominator a drop rate is drawn against in the
// ripple and rain family.
inline constexpr int IBN = 5100;

// WLED util.cpp:727. Returns a new, random colour wheel index with a minimum
// distance of 42 from pos.
uint8_t get_random_wheel_index(uint8_t pos);

/* WLED FX.cpp:87 keeps one file-static pseudo random generator that every effect
 * shares, so that saving and restoring its seed inside an effect leaves the same
 * sequence for the next one. Here it lives behind an accessor, so the hardware
 * random number generator is not read during static initialisation, before the
 * platform is up. */
Prng &fx_prng();

// WLED FX.h: #define SPEED_FORMULA_L (5U + (50U*(255U - SEGMENT.speed))/SEGLEN).
// A function rather than a macro, so it obeys the usual scoping rules.
inline unsigned speed_formula_l(const Segment &seg, unsigned seg_len) {
  return 5U + (50U * (255U - seg.speed)) / (seg_len ? seg_len : 1U);
}

// WLED FX.cpp:90. A sine that starts and stops at zero, with a flat gap for the
// second half of the input range.
inline uint8_t sin_gap(uint16_t in) {
  if (in & 0x100)
    return 0;
  return sin8_t(in + 192);  // correct phase shift of sine so that it starts and stops at 0
}

/* WLED FX.cpp:102. A tristate square wave with attack and decay.
 * @param x input value 0-255
 * @param pulsewidth 0-127
 * @param attdec attack & decay, max. pulsewidth / 2
 * @returns signed waveform value
 */
int8_t tristate_square8(uint8_t x, uint8_t pulsewidth, uint8_t attdec);

// WLED FX.cpp:2492 and 7125. Ripple state, one per concurrent ripple.
typedef struct Ripple {
  uint8_t state;
  uint8_t color;
  uint16_t pos;
} ripple;

// WLED FX.cpp:3255. Used by Popcorn, 1D Fireworks and Drip. Each needs 20 bytes.
typedef struct Spark {
  float pos, posX;
  float vel, velX;
  uint16_t col;
  uint8_t colIndex;
} spark;

// WLED FX.cpp:2580. Per-pixel flash state, 4 bytes, used by Fairytwinkle and Fairy.
typedef struct Flasher {
  uint16_t stateStart;
  uint8_t stateDur;
  bool stateOn;
} flasher;

// WLED FX.cpp:6800. State for the Grav* family.
typedef struct Gravity {
  int topLED;
  int gravityCounter;
} gravity;

/* WLED FX.cpp:195. Blink and strobe base: alternate between color1 and color2,
 * and if strobe is set make it a strobe rather than an even blink. */
void blink(Segment &seg, uint32_t color1, uint32_t color2, bool strobe, bool do_palette);

/* WLED FX.cpp:6806. Gravcenter effects by Andrew Tuline, merged into one base by
 * @dedehai. mode is Gravcenter (0), Gravcentric (1), Gravimeter (2),
 * Gravfreq (3). */
void mode_gravcenter_base(Segment &seg, unsigned mode);

// WLED FX.cpp:1948. Combined function from the original pride and colorwaves, by
// Mark Kriegsman.
void mode_colorwaves_pride_base(Segment &seg, bool isPride2015);

}  // namespace wled_fx
}  // namespace esphome
