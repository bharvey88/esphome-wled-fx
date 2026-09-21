// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "../wf_optimize.h"

#include "wled_fx_color_light.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.light";

namespace {

uint8_t to_byte(float value) {
  if (value <= 0.0f)
    return 0;
  if (value >= 1.0f)
    return 255;
  return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

}  // namespace

light::LightTraits WledFxColorLight::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void WledFxColorLight::write_state(light::LightState *state) {
  const light::LightColorValues &values = state->current_values;
  const float color_brightness = values.get_color_brightness();
  const uint8_t red = to_byte(values.get_red() * color_brightness);
  const uint8_t green = to_byte(values.get_green() * color_brightness);
  const uint8_t blue = to_byte(values.get_blue() * color_brightness);

  if (this->type_ == WledFxColorLightType::WLED_FX_COLOR_LIGHT_TYPE_COLOR1) {
    // The slot keeps its colour when the master is off, the way turning a lamp
    // off does not forget what colour it was.
    this->parent_->set_color_slot(0, RGBW32(red, green, blue, 0));
    this->parent_->set_output_brightness(to_byte(values.get_brightness()));
    this->parent_->set_output_enabled(values.get_state() > 0.0f);
    return;
  }

  // Colour 2 and colour 3 carry nothing but a colour, so off is black and the
  // brightness slider dims that colour.
  const float scale = values.get_state() * values.get_brightness();
  const unsigned slot = this->type_ == WledFxColorLightType::WLED_FX_COLOR_LIGHT_TYPE_COLOR2 ? 1 : 2;
  this->parent_->set_color_slot(slot, RGBW32(to_byte(red / 255.0f * scale), to_byte(green / 255.0f * scale),
                                             to_byte(blue / 255.0f * scale), 0));
}

void WledFxColorLight::dump_config() {
  ESP_LOGCONFIG(TAG, "WLED FX colour %u", static_cast<unsigned>(this->type_) + 1);
}

}  // namespace wled_fx
}  // namespace esphome
