#include "wled_fx_switch.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.switch";

void WledFxSwitch::setup() {
  /* With the platform's default restore mode, DISABLED, this returns nothing
   * and the engine's value is what gets published: either what YAML pinned or
   * what the effect's metadata says. Any other restore mode is a deliberate
   * choice to let the switch drive the engine at boot instead. */
  const optional<bool> restored = this->get_initial_state_with_restore_mode();
  if (restored.has_value())
    this->write_state(*restored);
  this->publish_current_();
  /* Changing the effect refills every checkmark the user did not pin from the
   * new effect's metadata defaults, so this entity is stale the moment anything
   * else moves unless it follows the engine. */
  this->parent_->add_on_state_change_callback([this]() { this->publish_current_(); });
}

void WledFxSwitch::publish_current_() {
  Segment &seg = this->parent_->engine().segment();
  bool current = false;
  switch (this->type_) {
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK1:
      current = seg.check1;
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK2:
      current = seg.check2;
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK3:
      current = seg.check3;
      break;
  }
  this->publish_state(current);
}

void WledFxSwitch::write_state(bool state) {
  Engine &engine = this->parent_->engine();
  switch (this->type_) {
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK1:
      engine.set_check1(state);
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK2:
      engine.set_check2(state);
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK3:
      engine.set_check3(state);
      break;
  }
  this->publish_state(state);
}

void WledFxSwitch::dump_config() { LOG_SWITCH("", "WLED FX Switch", this); }

}  // namespace wled_fx
}  // namespace esphome
