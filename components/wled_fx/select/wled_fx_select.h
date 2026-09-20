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

 protected:
  void control(const std::string &value) override;
  // Publishes whatever the engine currently has, if it is not already published.
  void publish_current_();

  WledFxSelectType type_;
  char *name_arena_{nullptr};  // holds the effect names the option list points at
};

}  // namespace wled_fx
}  // namespace esphome
