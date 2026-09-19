#include "wled_fx.h"

#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx";

bool WledFxController::set_effect_by_name(const std::string &name) { return this->engine_.set_effect(name.c_str()); }

bool WledFxController::set_palette_by_name(const std::string &name) {
  return this->engine_.set_palette_by_name(name.c_str());
}

void WledFxController::next_effect() {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return;
  this->engine_.set_effect_index((this->engine_.effect_index() + 1) % total);
}

std::string WledFxController::current_effect_name() const {
  const EffectInfo *info = this->engine_.effect();
  if (info == nullptr)
    return "";
  char buffer[64];
  effect_name(*info, buffer, sizeof(buffer));
  return buffer;
}

std::string WledFxController::current_palette_name() const {
  const uint8_t id = const_cast<Engine &>(this->engine_).segment().palette;
  if (id >= palette_count())
    return "";
  return PALETTE_NAMES[id];
}

void WledFxController::set_text_value(const std::string &text) {
  this->text_ = text;
  this->engine_.set_text(this->text_.c_str());
}

#ifdef USE_DISPLAY

void WledFxDisplay::setup() {
  if (this->display_ == nullptr) {
    ESP_LOGE(TAG, "No display");
    this->mark_failed();
    return;
  }
  if (this->width_ <= 0)
    this->width_ = this->display_->get_width();
  if (this->height_ <= 0)
    this->height_ = this->display_->get_height();
  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG, "Display reports a size of %dx%d, set width and height instead", this->width_, this->height_);
    this->mark_failed();
    return;
  }

  for (int i = 0; i < 256; i++) {
    this->gamma_lut_[i] = this->gamma_ == 1.0f
                              ? static_cast<uint8_t>(i)
                              : static_cast<uint8_t>(std::lround(std::pow(i / 255.0f, this->gamma_) * 255.0f));
  }

  if (!this->engine_.init(static_cast<uint16_t>(this->width_), static_cast<uint16_t>(this->height_))) {
    ESP_LOGE(TAG, "Canvas allocation failed for %dx%d", this->width_, this->height_);
    this->mark_failed();
    return;
  }

  RAMAllocator<uint8_t> allocator;
  this->frame_ = allocator.allocate(static_cast<size_t>(this->width_) * this->height_ * 3);
  if (this->frame_ == nullptr) {
    ESP_LOGE(TAG, "Frame buffer allocation failed");
    this->mark_failed();
    return;
  }
  this->engine_.set_text(this->text_.c_str());
}

void WledFxDisplay::update() {
  if (!this->enabled_ || this->frame_ == nullptr)
    return;
  this->engine_.render(App.get_loop_component_start_time());

  const Canvas &canvas = this->engine_.canvas();
  const size_t pixels = canvas.size();
  uint8_t *out = this->frame_;
  for (size_t i = 0; i < pixels; i++) {
    const uint32_t c = canvas.get(i);
    *out++ = this->gamma_lut_[(c >> 16) & 0xFF];
    *out++ = this->gamma_lut_[(c >> 8) & 0xFF];
    *out++ = this->gamma_lut_[c & 0xFF];
  }

  this->display_->draw_pixels_at(0, 0, this->width_, this->height_, this->frame_, display::COLOR_ORDER_RGB,
                                 display::COLOR_BITNESS_888, false);
  // Pushes the frame out. On hub75 with double buffering this is the flip.
  this->display_->update();
}

void WledFxDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "WLED FX display:\n"
                "  Canvas: %dx%d\n"
                "  Gamma: %.2f\n"
                "  Effects compiled in: %u\n"
                "  Effect: %s\n"
                "  Palette: %s",
                this->width_, this->height_, this->gamma_, static_cast<unsigned>(EffectRegistry::count()),
                this->current_effect_name().c_str(), this->current_palette_name().c_str());
  LOG_UPDATE_INTERVAL(this);
}

#endif  // USE_DISPLAY

}  // namespace wled_fx
}  // namespace esphome
