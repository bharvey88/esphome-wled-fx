#include "wled_fx_text_sensor.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.text_sensor";

/* Home Assistant refuses a state longer than 255 characters, and the longest
 * line any of the 223 effects produces is well under that, so one buffer this
 * size covers every effect with room to spare and the formatter truncates on a
 * whole item if a future effect ever exceeds it. */
static constexpr size_t LABEL_BUFFER = 256;

void WledFxTextSensor::setup() {
  this->publish_current_();
  this->parent_->add_on_state_change_callback([this]() { this->publish_current_(); });
}

void WledFxTextSensor::publish_current_() {
  const EffectInfo *info = this->parent_->engine().effect();
  if (info == nullptr || info == this->published_for_)
    return;
  this->published_for_ = info;

  char buffer[LABEL_BUFFER];
  if (this->type_ == WledFxTextSensorType::WLED_FX_TEXT_SENSOR_TYPE_CONTROLS)
    format_effect_controls(*info, buffer, sizeof(buffer));
  else
    format_effect_colors(*info, buffer, sizeof(buffer));
  this->publish_state(buffer);
}

void WledFxTextSensor::dump_config() { LOG_TEXT_SENSOR("", "WLED FX Text Sensor", this); }

}  // namespace wled_fx
}  // namespace esphome
