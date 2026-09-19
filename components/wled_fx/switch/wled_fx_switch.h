#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxSwitchType : uint8_t {
  WLED_FX_SWITCH_TYPE_CHECK1,
  WLED_FX_SWITCH_TYPE_CHECK2,
  WLED_FX_SWITCH_TYPE_CHECK3,
};

class WledFxSwitch : public switch_::Switch, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxSwitch(WledFxSwitchType type) : type_(type) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void write_state(bool state) override;

  WledFxSwitchType type_;
};

}  // namespace wled_fx
}  // namespace esphome
