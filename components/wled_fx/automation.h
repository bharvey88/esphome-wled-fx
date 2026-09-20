#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "wled_fx.h"

namespace esphome {
namespace wled_fx {

/* A literal name in YAML is checked at config time. A templated one cannot be,
 * so when the lambda produces something the build does not carry, say so rather
 * than do nothing. This is an automation, not the render loop, so a log line
 * here is not in any hot path. */
static const char *const ACTION_TAG = "wled_fx.action";

template<typename... Ts> class SetEffectAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  TEMPLATABLE_VALUE(std::string, effect)
  void play(const Ts &...x) override {
    const std::string name = this->effect_.value(x...);
    if (!this->parent_->set_effect_by_name(name))
      ESP_LOGW(ACTION_TAG, "'%s' is not an effect this build carries", name.c_str());
  }
};

template<typename... Ts> class NextEffectAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  void play(const Ts &...x) override { this->parent_->next_effect(); }
};

template<typename... Ts> class SetPaletteAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  TEMPLATABLE_VALUE(std::string, palette)
  void play(const Ts &...x) override {
    const std::string name = this->palette_.value(x...);
    if (!this->parent_->set_palette_by_name(name))
      ESP_LOGW(ACTION_TAG, "'%s' is not a palette this build carries", name.c_str());
  }
};

template<typename... Ts> class SetTextAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  TEMPLATABLE_VALUE(std::string, text)
  void play(const Ts &...x) override { this->parent_->set_text_value(this->text_.value(x...)); }
};

// One action covers every 0 to 255 slider; which one is picked at codegen time.
enum class ControlSlider : uint8_t {
  CONTROL_SLIDER_SPEED,
  CONTROL_SLIDER_INTENSITY,
  CONTROL_SLIDER_CUSTOM1,
  CONTROL_SLIDER_CUSTOM2,
  CONTROL_SLIDER_CUSTOM3,
};

template<typename... Ts> class SetSliderAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  explicit SetSliderAction(ControlSlider slider) : slider_(slider) {}
  TEMPLATABLE_VALUE(int, value)
  void play(const Ts &...x) override {
    const uint8_t v = static_cast<uint8_t>(this->value_.value(x...));
    switch (this->slider_) {
      case ControlSlider::CONTROL_SLIDER_SPEED:
        this->parent_->engine().set_speed(v);
        break;
      case ControlSlider::CONTROL_SLIDER_INTENSITY:
        this->parent_->engine().set_intensity(v);
        break;
      case ControlSlider::CONTROL_SLIDER_CUSTOM1:
        this->parent_->engine().set_custom1(v);
        break;
      case ControlSlider::CONTROL_SLIDER_CUSTOM2:
        this->parent_->engine().set_custom2(v);
        break;
      case ControlSlider::CONTROL_SLIDER_CUSTOM3:
        this->parent_->engine().set_custom3(v);
        break;
    }
    this->parent_->notify_state_change();
  }

 protected:
  ControlSlider slider_;
};

/* One of the three WLED colour slots. Which slot is picked at codegen time; the
 * three channels are templatable so a lambda or a Home Assistant value can
 * drive them. The light platform is the entity form of the same thing. */
template<typename... Ts> class SetColorAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  explicit SetColorAction(uint8_t slot) : slot_(slot) {}
  TEMPLATABLE_VALUE(int, red)
  TEMPLATABLE_VALUE(int, green)
  TEMPLATABLE_VALUE(int, blue)
  void play(const Ts &...x) override {
    const auto clamp = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    this->parent_->set_color_slot(this->slot_, RGBW32(clamp(this->red_.value(x...)), clamp(this->green_.value(x...)),
                                                      clamp(this->blue_.value(x...)), 0));
  }

 protected:
  uint8_t slot_;
};

enum class ControlCheck : uint8_t {
  CONTROL_CHECK_CHECK1,
  CONTROL_CHECK_CHECK2,
  CONTROL_CHECK_CHECK3,
};

template<typename... Ts> class SetCheckAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  explicit SetCheckAction(ControlCheck check) : check_(check) {}
  TEMPLATABLE_VALUE(bool, value)
  void play(const Ts &...x) override {
    const bool v = this->value_.value(x...);
    switch (this->check_) {
      case ControlCheck::CONTROL_CHECK_CHECK1:
        this->parent_->engine().set_check1(v);
        break;
      case ControlCheck::CONTROL_CHECK_CHECK2:
        this->parent_->engine().set_check2(v);
        break;
      case ControlCheck::CONTROL_CHECK_CHECK3:
        this->parent_->engine().set_check3(v);
        break;
    }
    this->parent_->notify_state_change();
  }

 protected:
  ControlCheck check_;
};

}  // namespace wled_fx
}  // namespace esphome
