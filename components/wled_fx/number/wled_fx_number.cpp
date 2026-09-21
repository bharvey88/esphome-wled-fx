// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "../wf_optimize.h"

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

void WledFxNumber::setup() {
  if (this->restore_) {
    this->pref_ = this->make_entity_preference<uint8_t>();
    uint8_t stored;
    if (this->pref_.load(&stored)) {
      // Through control(), so the engine takes it and pins it exactly as it
      // would a value moved from Home Assistant a moment after boot.
      this->control(stored);
    }
  }
  this->publish_current_();
  /* Changing the effect refills every control the user did not pin from the new
   * effect's metadata defaults, so this entity is stale the moment anything else
   * moves unless it follows the engine. */
  this->parent_->add_on_state_change_callback([this]() { this->publish_current_(); });
}

void WledFxNumber::publish_current_() {
  const float current = this->read_current_();
  if (!this->has_state() || this->state != current)
    this->publish_state(current);
}

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
  // set_custom3() clamps to 31, so republish what the engine took, not what
  // arrived.
  this->publish_current_();
  if (this->restore_) {
    // What the engine took, for the same reason.
    const uint8_t stored = this->read_current_();
    this->pref_.save(&stored);
  }
}

void WledFxNumber::dump_config() { LOG_NUMBER("", "WLED FX Number", this); }

}  // namespace wled_fx
}  // namespace esphome
