#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxSensorType : uint8_t {
  WLED_FX_SENSOR_TYPE_RENDER_TIME,
  WLED_FX_SENSOR_TYPE_OUTPUT_TIME,
};

/* The two halves of a frame's cost, in microseconds, averaged over the time the
 * current effect has been running. Render time is the effect function; output
 * time is everything from the canvas to the panel. An effect that is slow shows
 * it in the first; a display or a strip that is slow shows it in the second. */
class WledFxSensor : public sensor::Sensor, public PollingComponent, public Parented<WledFxController> {
 public:
  explicit WledFxSensor(WledFxSensorType type) : type_(type) {}

  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  WledFxSensorType type_;
};

}  // namespace wled_fx
}  // namespace esphome
