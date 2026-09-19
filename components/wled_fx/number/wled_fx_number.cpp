#include "wled_fx_number.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.number";

uint8_t WledFxNumber::read_current_() const {
  Segment &seg = const_cast<WledFxNumber *>(this)->parent_->engine().segment();
  switch (this->type_) {
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_SPEED:
      return seg.speed;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_INTENSITY:
      return seg.intensity;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM1:
      return seg.custom1;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM2:
      return seg.custom2;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM3:
      return seg.custom3;
  }
  return 0;
}

void WledFxNumber::setup() { this->publish_state(this->read_current_()); }

void WledFxNumber::control(float value) {
  const uint8_t v = static_cast<uint8_t>(value);
  Engine &engine = this->parent_->engine();
  switch (this->type_) {
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_SPEED:
      engine.set_speed(v);
      break;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_INTENSITY:
      engine.set_intensity(v);
      break;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM1:
      engine.set_custom1(v);
      break;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM2:
      engine.set_custom2(v);
      break;
    case WledFxNumberType::WLED_FX_NUMBER_TYPE_CUSTOM3:
      engine.set_custom3(v);
      break;
  }
  this->publish_state(value);
}

void WledFxNumber::dump_config() { LOG_NUMBER("", "WLED FX Number", this); }

}  // namespace wled_fx
}  // namespace esphome
