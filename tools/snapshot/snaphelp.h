#pragma once

/* Driver for the snapshot harness in tools/snapshot/port.yaml.
 *
 * None of this is part of the component, and nothing here touches the render
 * path. It is the bookkeeping the capture needs that will not fit on one line
 * of a YAML lambda: which effects this process was asked for, when to start and
 * stop capturing, what to call each frame, and a manifest saying which file was
 * taken at which millisecond.
 *
 * The frames themselves go through the real thing. The YAML builds a
 * `wled_fx` display front end over an ESPHome `snapshot` display, so a frame
 * that lands in a .bmp here has been through codegen, the frame gate,
 * draw_pixels_at() and the display's own update() exactly as it would on a
 * HUB75 panel.
 *
 * Everything is configured through the environment rather than the YAML, so one
 * compiled binary runs as many worker processes as there are cores, each with
 * its own slice of the effect list and its own output directory:
 *
 *   WFX_SNAP_EFFECTS     comma separated effect names, in the order to capture
 *   WFX_SNAP_MANIFEST    path to write the frame manifest to
 *   WFX_SNAP_SECONDS     how long to capture each effect, default 6
 *   WFX_SNAP_SETTLE_MS   how long to let an effect run before capturing, default 1500
 *   WFX_SNAP_PERIOD_MS   milliseconds between captured frames, default 50
 *   ESPHOME_SNAPSHOT_DIR where the .bmp files go, read by the snapshot component
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/wled_fx/wled_fx.h"

namespace wfxsnap {

static const char *const TAG = "wfxsnap";

/* WLED's factory segment colours, which is what the reference device was
 * captured with: an amber primary, and nothing in the other two slots. Many
 * effects paint the primary colour directly, so capturing the two sides with
 * different colours would report a hue difference on a couple of dozen effects
 * that are in fact rendering the same thing. */
inline constexpr uint32_t COLOR_PRIMARY = 0x00FFA000;
inline constexpr uint32_t COLOR_SECONDARY = 0x00000000;
inline constexpr uint32_t COLOR_TERTIARY = 0x00000000;

struct State {
  std::vector<std::string> effects;
  size_t current{0};
  uint32_t frame{0};
  uint32_t capture_end_ms{0};
  uint32_t next_due_ms{0};
  std::string manifest_path;
  FILE *manifest{nullptr};
  float seconds{6.0f};
  uint32_t settle_ms{1500};
  uint32_t period_ms{50};
};

inline State &state() {
  static State s;
  return s;
}

inline const char *env_or(const char *name, const char *fallback) {
  const char *value = getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return value != nullptr && value[0] != '\0' ? value : fallback;
}

/// Reads the environment and opens the manifest. Called once, from on_boot.
inline void setup() {
  State &s = state();
  s.seconds = strtof(env_or("WFX_SNAP_SECONDS", "6"), nullptr);
  s.settle_ms = static_cast<uint32_t>(strtoul(env_or("WFX_SNAP_SETTLE_MS", "1500"), nullptr, 10));
  s.period_ms = static_cast<uint32_t>(strtoul(env_or("WFX_SNAP_PERIOD_MS", "50"), nullptr, 10));
  if (s.period_ms == 0)
    s.period_ms = 50;

  // Comma separated, because an effect name can contain a space but never a
  // comma: "PS GEQ 2D", "Sparkle+", "Solid Pattern Tri".
  const std::string list = env_or("WFX_SNAP_EFFECTS", "");
  size_t start = 0;
  while (start <= list.size() && !list.empty()) {
    const size_t comma = list.find(',', start);
    const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!item.empty())
      s.effects.push_back(item);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }

  s.manifest_path = env_or("WFX_SNAP_MANIFEST", "");
  if (!s.manifest_path.empty()) {
    s.manifest = fopen(s.manifest_path.c_str(), "w");
    if (s.manifest == nullptr)
      ESP_LOGE(TAG, "Could not open the manifest at %s", s.manifest_path.c_str());
  }
  ESP_LOGI(TAG, "capturing %u effect(s), %.1f s each at one frame per %u ms",
           static_cast<unsigned>(s.effects.size()), s.seconds, s.period_ms);
}

inline bool has_more() { return state().current < state().effects.size(); }

/// Selects the next effect and its colours, and arms the capture window.
inline void begin_effect(esphome::wled_fx::WledFxController *ctrl) {
  State &s = state();
  const std::string &name = s.effects[s.current];

  /* Selecting by name reapplies the effect's own metadata defaults to every
   * control nothing pinned, which is exactly what WLED's `fxdef: true` does and
   * is how the reference device was driven. Nothing in port.yaml pins a
   * control, so nothing survives from the previous effect. */
  if (!ctrl->set_effect_by_name(name))
    ESP_LOGE(TAG, "'%s' was not selectable", name.c_str());

  // fxdef leaves the colours alone, so they are set here instead, every time,
  // rather than relying on nothing having moved them.
  ctrl->set_color_slot(0, COLOR_PRIMARY);
  ctrl->set_color_slot(1, COLOR_SECONDARY);
  ctrl->set_color_slot(2, COLOR_TERTIARY);

  s.frame = 0;
  ESP_LOGI(TAG, "[%u/%u] %s", static_cast<unsigned>(s.current + 1), static_cast<unsigned>(s.effects.size()),
           name.c_str());
}

/// Called after the settle delay, to start the clock on the capture window.
inline void arm_capture() {
  State &s = state();
  const uint32_t now = esphome::millis();
  s.capture_end_ms = now + static_cast<uint32_t>(s.seconds * 1000.0f);
  s.next_due_ms = now;
}

inline bool capturing() { return static_cast<int32_t>(state().capture_end_ms - esphome::millis()) > 0; }

/// The file name for the frame about to be taken, and the manifest line for it.
inline std::string frame_name() {
  State &s = state();
  const uint32_t now = esphome::millis();
  char name[64];
  snprintf(name, sizeof(name), "e%03u_f%04u.bmp", static_cast<unsigned>(s.current), s.frame);
  if (s.manifest != nullptr) {
    // Tab separated: an effect name can contain anything except a tab.
    fprintf(s.manifest, "%u\t%s\t%u\t%u\t%s\n", static_cast<unsigned>(s.current), s.effects[s.current].c_str(),
            s.frame, now, name);
  }
  s.frame++;
  return name;
}

inline void end_effect() {
  State &s = state();
  ESP_LOGI(TAG, "  %u frames", s.frame);
  s.current++;
}

/// Flushes the manifest and ends the process, which is what stops the run.
inline void finish() {
  State &s = state();
  if (s.manifest != nullptr) {
    fclose(s.manifest);
    s.manifest = nullptr;
  }
  ESP_LOGI(TAG, "done");
  fflush(stdout);
  // A host build has no reset, and the capture is over, so leave rather than
  // spin in a loop nothing will ever break out of.
  exit(0);  // NOLINT(concurrency-mt-unsafe)
}

}  // namespace wfxsnap
