#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../wled_fx.h"

namespace esphome {
namespace wled_fx {

enum class WledFxSelectType : uint8_t {
  WLED_FX_SELECT_TYPE_EFFECT,
  WLED_FX_SELECT_TYPE_PALETTE,
};

/* Populates its option list at setup from whatever was compiled in, so the
 * dropdown in Home Assistant always matches the firmware. */
class WledFxSelect : public select::Select, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxSelect(WledFxSelectType type) : type_(type) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  /* Keeps the choice across a reboot instead of taking the effect's own default
   * palette again. See the same option on WledFxNumber for why it is off unless
   * a configuration asks for it. The stored value is the position in the option
   * list, so a build whose option list changed falls back to whatever the
   * engine has rather than to something arbitrary. */
  void set_restore_value(bool restore) { this->restore_ = restore; }

 protected:
  void control(const std::string &value) override;
  // Publishes whatever the engine currently has, if it is not already published.
  void publish_current_();

  WledFxSelectType type_;
  char *name_arena_{nullptr};  // holds the effect names the option list points at
  bool restore_{false};
  ESPPreferenceObject pref_;
};

}  // namespace wled_fx
}  // namespace esphome
