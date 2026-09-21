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

  /* Keeps the value across a reboot instead of taking the effect's own default
   * again. Off unless a configuration asks for it, because the engine owns this
   * value: it comes from YAML if it was pinned there and from the effect's
   * metadata otherwise. A restored value is applied as though somebody had just
   * moved the control, so it is pinned the same way. Costs one byte of flash
   * storage and nothing per frame. */
  void set_restore_value(bool restore) { this->restore_ = restore; }

 protected:
  void control(float value) override;
  uint8_t read_current_() const;
  // Publishes whatever the engine currently has, if it is not already published.
  void publish_current_();

  WledFxNumberType type_;
  bool restore_{false};
  ESPPreferenceObject pref_;
};

}  // namespace wled_fx
}  // namespace esphome
