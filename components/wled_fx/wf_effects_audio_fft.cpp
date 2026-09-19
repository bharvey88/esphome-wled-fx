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

#include "wf_effects.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in. The group is empty for now, so the list is empty too.
#define WLED_FX_GROUP_AUDIO_FFT (WLED_FX_DEFAULT_ENABLE)

#if WLED_FX_GROUP_AUDIO_FFT

namespace esphome {
namespace wled_fx {
namespace {

// Effect bodies go here, each wrapped in its own
// `#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_<NAME>`. Shared upstream helpers
// (chase, ripple_base, ...) are copied into this file as static functions in the
// anonymous namespace rather than shared with another translation unit.

/* Registration table. While this group is empty it is a null pointer with a count
 * of zero, because a zero length array is not valid C++. As soon as you add the
 * first effect, replace the next two lines with:
 *
 *   const EffectInfo ENTRIES[] = {
 *   #if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TWINKLE
 *       {"Twinkle@!,!;!,!;!;;m12=0", mode_twinkle},
 *   #endif
 *   };
 *   constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);
 */
const EffectInfo *const ENTRIES = nullptr;
constexpr size_t ENTRY_COUNT = 0;

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_AUDIO_FFT;
const EffectGroup EFFECT_GROUP_AUDIO_FFT{"audio_fft", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_AUDIO_FFT
