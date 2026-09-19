#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LIGHT

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "wled_fx.h"

namespace esphome {
namespace wled_fx {

/* Addressable light front end. The canvas is width x height; for a plain strip
 * that is size() x 1. A matrix wired as one strip is described with width, height
 * and serpentine, matching the vocabulary the addressable_light display platform
 * already uses. */
class WledFxLightEffect : public light::AddressableLightEffect, public WledFxController {
 public:
  explicit WledFxLightEffect(const char *name) : light::AddressableLightEffect(name) {}

  void set_dimensions(int width, int height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_serpentine(bool serpentine) { this->serpentine_ = serpentine; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ = interval_ms; }
  void set_use_light_color(bool use) { this->use_light_color_ = use; }

  void start() override;
  void stop() override;
  void apply(light::AddressableLight &it, const Color &current_color) override;

 protected:
  int width_{0};
  int height_{0};
  bool serpentine_{false};
  bool use_light_color_{true};
  uint32_t frame_interval_{33};
  uint32_t last_frame_{0};
  bool ready_{false};
};

}  // namespace wled_fx
}  // namespace esphome

#endif  // USE_LIGHT
