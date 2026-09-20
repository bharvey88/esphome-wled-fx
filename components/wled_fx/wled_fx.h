#pragma once

#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "wf_engine.h"

#ifdef USE_DISPLAY
#include "esphome/components/display/display.h"
#endif

namespace esphome {
namespace wled_fx {

/* Shared runtime surface. Both front ends own an Engine and expose the same
 * controls, so the actions and the select / number / switch platforms work
 * against either one. */
class WledFxController {
 public:
  Engine &engine() { return this->engine_; }

  // Returns false when the effect or palette name is not compiled in.
  bool set_effect_by_name(const std::string &name);
  bool set_palette_by_name(const std::string &name);
  void next_effect();

  std::string current_effect_name() const;
  std::string current_palette_name() const;

  void set_text_value(const std::string &text);

  /* The frame clock. ESPHome's main loop ticks about every 16 ms and its
   * scheduler re-arms an interval from the moment the callback ran, so a 23 ms
   * interval becomes 32 ms in practice, and every effect runs at 31 fps instead
   * of the 42 fps WLED's FRAMETIME means. Speeds are visibly wrong.
   *
   * Accumulating the deadline instead of re-arming it fixes that: the gate
   * alternates between one tick and two, and the average comes out at the
   * configured period. If the loop stalls long enough that the deadline is more
   * than one period in the past, it resynchronises rather than trying to catch
   * up with a burst of frames nobody would see.
   *
   * Both front ends use this, so the light effect and the display front end run
   * at the same rate from the same code. */
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ = interval_ms; }
  uint32_t frame_interval() const { return this->frame_interval_; }

  // Restarts the clock so the next call to due_() renders immediately.
  void reset_frame_clock() { this->have_deadline_ = false; }

  /* Changing the effect refills every control the user did not pin from the new
   * effect's own metadata defaults, so a `select`, `number` or `switch` entity
   * that published its value at setup is stale the moment anything else moves.
   * Those entities subscribe here and republish. The callbacks run on the main
   * loop, from whatever changed the state, never per frame. */
  void add_on_state_change_callback(std::function<void()> &&callback) {
    this->state_change_callbacks_.push_back(std::move(callback));
  }
  // Called by whatever touched the engine outside this class, the actions.
  void notify_state_change() {
    for (auto &callback : this->state_change_callbacks_)
      callback();
  }

 protected:
  // True when `now` has reached the next frame deadline, which it then advances.
  bool frame_due_(uint32_t now);

  Engine engine_;
  std::string text_;
  std::vector<std::function<void()>> state_change_callbacks_;
  uint32_t frame_interval_{FRAMETIME};
  uint32_t next_frame_{0};
  bool have_deadline_{false};
};

#ifdef USE_DISPLAY
/* Display front end. Owns the frame clock, renders into the canvas and pushes the
 * whole frame with a single draw_pixels_at() call.
 *
 * hub75 only flips its double buffer inside update(), and update() is never called
 * when the display has `update_interval: never`, so this component calls update()
 * itself right after the blit. With `auto_clear_enabled: false` and no lambda or
 * pages on the display, that update() is just the buffer flip. Both of those
 * display settings are enforced at codegen time. */
class WledFxDisplay : public Component, public WledFxController {
 public:
  void set_display(display::Display *display) { this->display_ = display; }
  void set_dimensions(int width, int height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_gamma(float gamma) { this->gamma_ = gamma; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
  bool is_enabled() const { return this->enabled_; }

  void setup() override;
  /* A plain loop(), not a PollingComponent's update(). ESPHome's scheduler
   * re-arms an interval from the moment the callback ran, which on a 16 ms main
   * loop turns a 23 ms interval into 32 ms and every effect into a 31 fps
   * version of itself. The gate in WledFxController accumulates instead, so the
   * average rate is the one that was asked for. */
  void loop() override;
  void dump_config() override;
  // Runs after the display driver, which needs to be up before get_width() works.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  display::Display *display_{nullptr};
  uint8_t *frame_{nullptr};
  int width_{0};
  int height_{0};
  float gamma_{1.0f};
  uint8_t gamma_lut_[256]{};
  bool enabled_{true};
};
#endif  // USE_DISPLAY

}  // namespace wled_fx
}  // namespace esphome
