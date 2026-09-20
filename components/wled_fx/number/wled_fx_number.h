#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxNumberType : uint8_t {
  WLED_FX_NUMBER_TYPE_SPEED,
  WLED_FX_NUMBER_TYPE_INTENSITY,
  WLED_FX_NUMBER_TYPE_CUSTOM1,
  WLED_FX_NUMBER_TYPE_CUSTOM2,
  WLED_FX_NUMBER_TYPE_CUSTOM3,
};

class WledFxNumber : public number::Number, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxNumber(WledFxNumberType type) : type_(type) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void control(float value) override;
  uint8_t read_current_() const;
  // Publishes whatever the engine currently has, if it is not already published.
  void publish_current_();

  WledFxNumberType type_;
};

}  // namespace wled_fx
}  // namespace esphome
