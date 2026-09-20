#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxTextSensorType : uint8_t {
  WLED_FX_TEXT_SENSOR_TYPE_CONTROLS,
  WLED_FX_TEXT_SENSOR_TYPE_COLORS,
};

/* Publishes the WLED labels for the controls the running effect actually uses,
 * so that a generic "Custom 1" slider on a web page or a dashboard can be read
 * as what the effect calls it. Rebuilt on an effect change and at no other
 * time: the state change callback also fires when a slider moves, and the
 * labels cannot have changed then. */
class WledFxTextSensor : public text_sensor::TextSensor, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxTextSensor(WledFxTextSensorType type) : type_(type) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void publish_current_();

  WledFxTextSensorType type_;
  const EffectInfo *published_for_{nullptr};
};

}  // namespace wled_fx
}  // namespace esphome
