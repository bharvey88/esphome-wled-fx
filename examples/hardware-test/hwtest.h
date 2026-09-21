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

#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_heap_caps.h"

#include "esphome/components/wled_fx/wf_audio.h"
#include "esphome/components/wled_fx/wf_registry.h"
#include "esphome/components/wled_fx/wled_fx.h"

namespace wledfx_hwtest {

using esphome::wled_fx::EffectInfo;
using esphome::wled_fx::EffectRegistry;
using esphome::wled_fx::WledFxController;

/* Tour filters, in the same order as the options of the "Tour group" select.
 * The select publishes its option index straight into the tour_filter global,
 * so the two lists have to stay in step. */
enum Filter : int {
  FILTER_ALL = 0,
  /* What a panel and a strip offer with no opt-in, which is the same rule the
   * component applies to a configuration that did not set
   * `include_1d_effects`. These firmwares do set it, so that the tour and the
   * profile run can reach all 223, and the price is that a 1D effect like
   * Gradient on a 64x64 panel is a short line crawling along a 4096 pixel
   * strip: faithful to WLED, and not what anybody wants to see first. "Panel"
   * is the group that hides those, and it is the default on the two matrix
   * configs. */
  FILTER_PANEL,
  FILTER_STRIP,
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
    // Section 2, the two effects that degrade above 180 pixels wide. Both look
    // ordinary at 64x64 and are only interesting on a panel wider than that.
    "Octopus", "Game Of Life",
    // Section 3, real microphone
    "Rocktaves", "Ripple Peak", "Puddlepeak", "DJ Light", "PS Spray", "PS Blobs",
};

/* The group an effect belongs to. Registry indices are the groups concatenated
 * in link order, which is what EffectRegistry::at() walks, so the same walk
 * gives the group name back.
 *
 * The names are the ones the wf_effects_*.cpp files register: 1d2d, 1d_a to
 * 1d_e, 2d_a, 2d_b, audio_fft, audio_particle, audio_vol, mm, particle_1d and
 * particle_2d. matches() below tests prefixes of those, so a build with an
 * `effects:` allow-list, which links only some of the groups, still sorts
 * correctly. */
inline const char *group_of(size_t index) {
  for (size_t g = 0; g < EffectRegistry::group_count(); g++) {
    const auto &group = EffectRegistry::group(g);
    if (index < group.count)
      return group.group_name;
    index -= group.count;
  }
  return "?";
}

/* True when this effect is only offered here because the firmware opted in to
 * the 1D effects on a 2D output. Those are the ones that look broken on a
 * panel until you know what they are, so the tour says so. */
inline bool strip_only_on_panel(const WledFxController *ctrl, size_t index) {
  if (ctrl == nullptr || !ctrl->layout_2d())
    return false;
  const EffectInfo *info = EffectRegistry::at(index);
  return info != nullptr && !esphome::wled_fx::effect_available(*info, true, false);
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

/* Every one of these takes the controller, because which effects exist is not
 * the same question as which ones this output can run. A 2D-only effect on a
 * strip, or a 1D-only one on a matrix without `include_1d_effects: true`, is
 * not offered: the component refuses to select it and the select entity does
 * not list it, so a tour that walked onto it would show a panel of nothing and
 * profile an effect the user cannot reach. The layout filter comes first and
 * the group filters narrow what is left, so on a 2D output the "1D" group is
 * the 1D effects that this output actually offers, which with the opt-in off
 * is none of them. */
inline bool matches(const WledFxController *ctrl, int filter, size_t index) {
  if (ctrl != nullptr && !ctrl->effect_offered(index))
    return false;
  const EffectInfo *info = EffectRegistry::at(index);
  const char *group = group_of(index);
  switch (filter) {
    case FILTER_PANEL:
      // The 2D-capable effects, read from the same effect_available() the
      // component uses, not from a list typed out here.
      return info != nullptr && esphome::wled_fx::effect_available(*info, true, false);
    case FILTER_STRIP:
      return info != nullptr && esphome::wled_fx::effect_available(*info, false, false);
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
inline size_t count(const WledFxController *ctrl, int filter) {
  const size_t total = EffectRegistry::count();
  size_t n = 0;
  for (size_t i = 0; i < total; i++) {
    if (matches(ctrl, filter, i))
      n++;
  }
  return n;
}

// Where `index` sits in the filtered list, 1 based. Zero when it is filtered out.
inline size_t position(const WledFxController *ctrl, int filter, size_t index) {
  if (!matches(ctrl, filter, index))
    return 0;
  size_t n = 0;
  for (size_t i = 0; i <= index; i++) {
    if (matches(ctrl, filter, i))
      n++;
  }
  return n;
}

/* The next index the filter accepts, walking forwards for delta >= 0 and
 * backwards otherwise. Returns `from` unchanged when nothing matches, which
 * leaves the current effect running rather than blanking the panel. That is
 * what a group with nothing in it looks like: "2D" on a strip, or "1D" on a
 * matrix that did not opt in. */
inline size_t step(const WledFxController *ctrl, int filter, size_t from, int delta) {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return from;
  const size_t increment = delta >= 0 ? 1 : total - 1;  // total - 1 is -1 modulo total
  size_t index = from;
  for (size_t tries = 0; tries < total; tries++) {
    index = (index + increment) % total;
    if (matches(ctrl, filter, index))
      return index;
  }
  return from;
}

// The current effect if the filter accepts it, otherwise the next one that fits.
inline size_t first(const WledFxController *ctrl, int filter, size_t from) {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return from;
  return matches(ctrl, filter, from % total) ? from % total : step(ctrl, filter, from, 1);
}

// Zero on a board with no PSRAM, which is the honest answer rather than an error.
inline size_t psram_free() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

/* --- tour dwell ------------------------------------------------------------
 *
 * The same two effects the simulator's PACING table gives extra time to, for
 * the same reason: they are paced so slowly that the dwell a profiling run
 * wants, five seconds, never reaches the part worth looking at. Sunrise spends
 * the first quarter of an hour-long sunrise black, and PS Galaxy needs several
 * hundred frames before the arms appear. A multiplier rather than a fixed
 * dwell, so turning the dwell up still turns these up with it. */
struct PacedEffect {
  const char *effect;
  uint8_t dwell_multiplier;
};

inline const PacedEffect PACED[] = {
    {"Sunrise", 6},
    {"PS Galaxy", 6},
};

inline uint8_t dwell_multiplier(size_t index) {
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return 1;
  for (const PacedEffect &paced : PACED) {
    if (esphome::wled_fx::effect_name_equals(*info, paced.effect))
      return paced.dwell_multiplier;
  }
  return 1;
}

/* --- profiling -------------------------------------------------------------
 *
 * One line per effect, written when the tour leaves it, in a shape a script can
 * read back: tools/parse_profile_log.py turns a captured log into a table. The
 * whole line is built once every dwell, never per frame. */
inline std::string result_line(esphome::wled_fx::WledFxController *ctrl, size_t index, unsigned tour_index) {
  const auto &stats = ctrl->profile();
  const std::string name = ctrl->current_effect_name();
  const uint32_t fps10 = stats.fps_x10();
  char line[320];
  snprintf(line, sizeof(line),
           "[tour] RESULT idx=%u name=\"%s\" group=%s frames=%" PRIu32 " fps=%" PRIu32 ".%" PRIu32
           " render_us=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " out_us=%" PRIu32 "/%" PRIu32 "/%" PRIu32
           " heap=%u largest=%u psram=%u data_bytes=%u",
           tour_index, name.c_str(), group_of(index), stats.frames, fps10 / 10, fps10 % 10, stats.render_avg_us(),
           stats.render_min_us, stats.render_max_us, stats.output_avg_us(), stats.output_min_us, stats.output_max_us,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned) psram_free(),
           (unsigned) ctrl->engine().segment().data_size());
  return line;
}

/* Nothing here wraps the controller any more. The component owns all three of
 * the things this file used to work around, and the YAML calls them directly:
 *
 *   ctrl->notify_state_change()      the select, number and switch entities
 *                                    republish after something moved the engine
 *   ctrl->engine().restart_effect()  current effect back to frame zero
 *   ctrl->current_effect_name()      the display name of what is running
 *
 * Refilling the controls from the effect's own metadata is still a reselect of
 * the current index: restart_effect() resets the segment, and only an effect
 * change reapplies the defaults. The "Unpin controls" button in tour.yaml is
 * the one place that wants that. */

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
