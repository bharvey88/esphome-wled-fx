#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxColorLightType : uint8_t {
  WLED_FX_COLOR_LIGHT_TYPE_COLOR1,
  WLED_FX_COLOR_LIGHT_TYPE_COLOR2,
  WLED_FX_COLOR_LIGHT_TYPE_COLOR3,
};

/* One WLED colour slot as an ESPHome light, which is the only entity type that
 * renders as a colour picker in web_server and in Home Assistant. WLED's UI
 * calls the three slots Fx, Bg and Cs; which of them an effect uses, and what
 * it calls them, is in the effect's metadata and is published by the wled_fx
 * text sensor.
 *
 * Colour 1 is also the master, as it is in WLED: its brightness scales the
 * whole finished frame and turning it off blanks the panel, while the colour
 * it holds is taken at full brightness so a dim panel does not also mean a dim
 * colour 1. Colour 2 and colour 3 are plain slots, so their brightness dims
 * that colour and turning one off makes it black, which is how an effect is
 * told it has no background or no custom colour. */
class WledFxColorLight : public light::LightOutput, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxColorLight(WledFxColorLightType type) : type_(type) {}

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  WledFxColorLightType type_;
};

}  // namespace wled_fx
}  // namespace esphome
