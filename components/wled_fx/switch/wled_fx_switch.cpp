#include "wled_fx_switch.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.switch";

void WledFxSwitch::setup() {
  Segment &seg = this->parent_->engine().segment();
  switch (this->type_) {
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK1:
      this->publish_state(seg.check1);
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK2:
      this->publish_state(seg.check2);
      break;
    case WledFxSwitchType::WLED_FX_SWITCH_TYPE_CHECK3:
      this->publish_state(seg.check3);
      break;
  }
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
