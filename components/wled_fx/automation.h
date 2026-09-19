#pragma once

#include "esphome/core/automation.h"
#include "wled_fx.h"

namespace esphome {
namespace wled_fx {

template<typename... Ts> class SetEffectAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  TEMPLATABLE_VALUE(std::string, effect)
  void play(const Ts &...x) override { this->parent_->set_effect_by_name(this->effect_.value(x...)); }
};

template<typename... Ts> class NextEffectAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  void play(const Ts &...x) override { this->parent_->next_effect(); }
};

template<typename... Ts> class SetPaletteAction : public Action<Ts...>, public Parented<WledFxController> {
 public:
  TEMPLATABLE_VALUE(std::string, palette)
  void play(const Ts &...x) override { this->parent_->set_palette_by_name(this->palette_.value(x...)); }
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
  }

 protected:
  ControlSlider slider_;
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
  }

 protected:
  ControlCheck check_;
};

}  // namespace wled_fx
}  // namespace esphome
