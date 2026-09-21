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

/* Which slice of the offered effects an `effect` select lists.
 *
 * ALL is the whole of what this output offers, which is the only thing that
 * existed before and is still the default.
 *
 * The other two exist for the case `include_1d_effects: true` creates. On a
 * matrix that has opted in, the offered list is every effect in the build, and
 * 159 of the 223 are 1D-only: they reach the panel through WLED's 1D to 2D
 * mapping and most of them are a short line crawling along a 4096 pixel strip
 * wrapped across the panel. That is faithful and it is not what somebody
 * scrolling a dropdown is looking for. PANEL is the 64 an untouched 2D output
 * would offer; STRIP is what a 1D output would offer. Both read
 * effect_available() rather than a list, so they are the same rule the
 * component enforces and the same one the hardware-test tour groups use.
 *
 * A configuration can therefore give a panel two dropdowns, one of the effects
 * that suit it and one clearly labelled as the mapped strip effects, instead of
 * one list of 223 in which the two kinds are indistinguishable. */
enum class WledFxSelectScope : uint8_t {
  WLED_FX_SELECT_SCOPE_ALL,
  WLED_FX_SELECT_SCOPE_PANEL,
  WLED_FX_SELECT_SCOPE_STRIP,
};

/* Populates its option list at setup from whatever was compiled in, so the
 * dropdown in Home Assistant always matches the firmware. */
class WledFxSelect : public select::Select, public Component, public Parented<WledFxController> {
 public:
  explicit WledFxSelect(WledFxSelectType type) : type_(type) {}

  void set_scope(WledFxSelectScope scope) { this->scope_ = scope; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void control(const std::string &value) override;
  // Publishes whatever the engine currently has, if it is not already published.
  void publish_current_();
  // True when this select lists the effect at `index`.
  bool in_scope_(size_t index) const;

  WledFxSelectType type_;
  WledFxSelectScope scope_{WledFxSelectScope::WLED_FX_SELECT_SCOPE_ALL};
  char *name_arena_{nullptr};  // holds the effect names the option list points at
};

}  // namespace wled_fx
}  // namespace esphome
