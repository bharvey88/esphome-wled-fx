#pragma once

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

 protected:
  Engine engine_;
  std::string text_;
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
class WledFxDisplay : public PollingComponent, public WledFxController {
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
  void update() override;
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
