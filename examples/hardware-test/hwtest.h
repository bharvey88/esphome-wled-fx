#pragma once

/* Helpers for the wled_fx hardware test firmwares in examples/hardware-test.
 *
 * None of this is part of the component. It is the handful of things the test
 * YAML needs that do not fit on one line of a lambda: mapping a registry index
 * to its effect group, filtering the tour down to one group, and the list of
 * effects HARDWARE-CHECKLIST.md asks about.
 *
 * Pulled into a build with:
 *
 *   esphome:
 *     includes:
 *       - hwtest.h
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_heap_caps.h"

#include "esphome/components/wled_fx/wf_audio.h"
#include "esphome/components/wled_fx/wf_registry.h"

namespace wledfx_hwtest {

using esphome::wled_fx::EffectInfo;
using esphome::wled_fx::EffectRegistry;

/* Tour filters, in the same order as the options of the "Tour group" select.
 * The select publishes its option index straight into the tour_filter global,
 * so the two lists have to stay in step. */
enum Filter : int {
  FILTER_ALL = 0,
  FILTER_1D,
  FILTER_2D,
  FILTER_PARTICLE_2D,
  FILTER_PARTICLE_1D,
  FILTER_AUDIO,
  FILTER_MM,
  FILTER_CHECKLIST,
};

/* Every effect HARDWARE-CHECKLIST.md names, so one pass of the "Checklist"
 * filter covers the open questions and nothing else. Names are the WLED display
 * names and the comparison is case insensitive. A name that is not compiled in
 * is simply never matched, which is what makes this list safe on a build with
 * an `effects:` allow-list. */
inline const char *const CHECKLIST[] = {
    // Section 1, any strip
    "Sunrise", "Color Clouds", "Slow Transition", "Halloween Eyes", "Traffic Light", "Strobe Mega", "Noise Pal",
    "Oscillate",
    // Section 2, matrix panel
    "Bouncing Balls", "Tetrix", "Rolling Balls", "PS Sonic Stream", "PS Sonic Boom", "PS Springy", "Polar Lights",
    "Firenoise", "Meteor Smooth", "Blobs", "PS Galaxy", "PS Attractor", "Paintbrush", "PS Pinball",
    // Section 3, real microphone
    "Rocktaves", "Ripple Peak", "Puddlepeak", "DJ Light", "PS Spray", "PS Blobs",
};

/* The group an effect belongs to. Registry indices are the groups concatenated
 * in link order, which is what EffectRegistry::at() walks, so the same walk
 * gives the group name back. */
inline const char *group_of(size_t index) {
  for (size_t g = 0; g < EffectRegistry::group_count(); g++) {
    const auto &group = EffectRegistry::group(g);
    if (index < group.count)
      return group.group_name;
    index -= group.count;
  }
  return "?";
}

inline std::string name_of(size_t index) {
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return "";
  char buffer[64];
  esphome::wled_fx::effect_name(*info, buffer, sizeof(buffer));
  return buffer;
}

inline bool on_checklist(size_t index) {
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return false;
  for (const char *name : CHECKLIST) {
    if (esphome::wled_fx::effect_name_equals(*info, name))
      return true;
  }
  return false;
}

inline bool matches(int filter, size_t index) {
  const char *group = group_of(index);
  switch (filter) {
    case FILTER_1D:
      // 1d_a through 1d_e, and 1d2d, whose effects all run on a strip too.
      return strncmp(group, "1d", 2) == 0;
    case FILTER_2D:
      return strncmp(group, "2d", 2) == 0;
    case FILTER_PARTICLE_2D:
      return strcmp(group, "particle_2d") == 0;
    case FILTER_PARTICLE_1D:
      return strcmp(group, "particle_1d") == 0;
    case FILTER_AUDIO:
      return strncmp(group, "audio", 5) == 0;
    case FILTER_MM:
      return strcmp(group, "mm") == 0;
    case FILTER_CHECKLIST:
      return on_checklist(index);
    default:
      return true;
  }
}

// How many effects the filter lets through.
inline size_t count(int filter) {
  const size_t total = EffectRegistry::count();
  size_t n = 0;
  for (size_t i = 0; i < total; i++) {
    if (matches(filter, i))
      n++;
  }
  return n;
}

// Where `index` sits in the filtered list, 1 based. Zero when it is filtered out.
inline size_t position(int filter, size_t index) {
  if (!matches(filter, index))
    return 0;
  size_t n = 0;
  for (size_t i = 0; i <= index; i++) {
    if (matches(filter, i))
      n++;
  }
  return n;
}

/* The next index the filter accepts, walking forwards for delta >= 0 and
 * backwards otherwise. Returns `from` unchanged when nothing matches, which
 * leaves the current effect running rather than blanking the panel. */
inline size_t step(int filter, size_t from, int delta) {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return from;
  const size_t increment = delta >= 0 ? 1 : total - 1;  // total - 1 is -1 modulo total
  size_t index = from;
  for (size_t tries = 0; tries < total; tries++) {
    index = (index + increment) % total;
    if (matches(filter, index))
      return index;
  }
  return from;
}

// The current effect if the filter accepts it, otherwise the next one that fits.
inline size_t first(int filter, size_t from) {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return from;
  return matches(filter, from % total) ? from % total : step(filter, from, 1);
}

// Zero on a board with no PSRAM, which is the honest answer rather than an error.
inline size_t psram_free() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

/* Tells the select, number and switch entities to republish after the tour has
 * moved the engine behind their backs.
 *
 * WledFxController::notify_state_change() does not exist in every version of
 * the component, so this resolves to nothing on a build that lacks it and the
 * Effect name text sensor is then the only thing that follows the tour. The
 * harness is meant to survive the component changing under it. */
namespace detail {
template<typename T> auto notify(T *controller, int) -> decltype(controller->notify_state_change(), void()) {
  controller->notify_state_change();
}
template<typename T> void notify(T *, long) {}
}  // namespace detail

template<typename T> void notify_state_change(T *controller) { detail::notify(controller, 0); }

/* Puts the current effect back to frame zero. Selecting the same index again is
 * what does it: the engine resets the segment scratch and refills every control
 * the user has not pinned from the effect's own metadata. */
template<typename T> void restart_effect(T *controller) {
  controller->engine().set_effect_index(controller->engine().effect_index());
  notify_state_change(controller);
}

/* --- live audio ------------------------------------------------------------
 *
 * These read the attached microphone only, never WLED's simulated sound. A
 * build with no `audio:` block publishes nothing, which is the point: the
 * numbers are there to compare a real room against gain and squelch, and a
 * simulated value would quietly look plausible. */

// Null when no microphone is attached or the first block has not landed yet.
inline const esphome::wled_fx::AudioData *live_audio() {
  esphome::wled_fx::AudioSource *source = esphome::wled_fx::audio_source();
  return source != nullptr && source->has_data() ? &source->data() : nullptr;
}

inline float volume_smth() {
  const esphome::wled_fx::AudioData *audio = live_audio();
  return audio == nullptr ? NAN : audio->volume_smth;
}

inline float major_peak() {
  const esphome::wled_fx::AudioData *audio = live_audio();
  return audio == nullptr ? NAN : audio->fft_major_peak;
}

inline float sound_pressure() {
  const esphome::wled_fx::AudioData *audio = live_audio();
  return audio == nullptr ? NAN : audio->sound_pressure;
}

// 255 minus the AGC gain, which is the input level the controller settled on.
inline float agc_sensitivity() {
  const esphome::wled_fx::AudioData *audio = live_audio();
  return audio == nullptr ? NAN : audio->agc_sensitivity;
}

inline float geq(uint8_t band) {
  const esphome::wled_fx::AudioData *audio = live_audio();
  if (audio == nullptr || band >= esphome::wled_fx::NUM_GEQ_CHANNELS)
    return NAN;
  return audio->fft_result[band];
}

// All sixteen bands on one line, which is the form worth reading in a log.
inline std::string geq_row() {
  const esphome::wled_fx::AudioData *audio = live_audio();
  if (audio == nullptr)
    return "no microphone data";
  std::string out;
  char cell[8];
  for (uint8_t i = 0; i < esphome::wled_fx::NUM_GEQ_CHANNELS; i++) {
    snprintf(cell, sizeof(cell), i == 0 ? "%u" : " %u", audio->fft_result[i]);
    out += cell;
  }
  return out;
}

}  // namespace wledfx_hwtest
