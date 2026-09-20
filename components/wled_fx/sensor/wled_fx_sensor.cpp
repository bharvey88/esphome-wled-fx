#include "wled_fx_sensor.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.sensor";

void WledFxSensor::update() {
  const WledFxController::ProfileStats &stats = this->parent_->profile();
  /* The first second of an effect is left out of the statistics, so a window
   * that has not got past it yet has nothing honest to publish. Saying nothing
   * is better than publishing a zero that reads like a fast effect. */
  if (stats.frames == 0)
    return;
  const uint32_t value = this->type_ == WledFxSensorType::WLED_FX_SENSOR_TYPE_RENDER_TIME ? stats.render_avg_us()
                                                                                         : stats.output_avg_us();
  this->publish_state(static_cast<float>(value));
}

void WledFxSensor::dump_config() { LOG_SENSOR("", "WLED FX Sensor", this); }

}  // namespace wled_fx
}  // namespace esphome
