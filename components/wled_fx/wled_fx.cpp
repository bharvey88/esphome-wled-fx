#include "wled_fx.h"

#include <cinttypes>
#include <cmath>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx";

bool WledFxController::effect_offered(size_t index) const {
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return false;
  return effect_available(*info, this->layout_2d_, this->include_1d_effects_);
}

size_t WledFxController::first_offered_effect() const {
  const size_t total = EffectRegistry::count();
  for (size_t i = 0; i < total; i++) {
    if (this->effect_offered(i))
      return i;
  }
  return SIZE_MAX;
}

void WledFxController::ensure_offered_effect() {
  if (this->effect_offered(this->engine_.effect_index()))
    return;
  const size_t first = this->first_offered_effect();
  if (first == SIZE_MAX) {
    /* Config validation refuses a build in which no effect can be offered, so
     * reaching this means the guard there and the rule here disagree. Say so
     * rather than render whatever index 0 happens to be. */
    ESP_LOGE(TAG, "No compiled-in effect can run on a %s output", this->layout_2d_ ? "2D" : "1D");
    return;
  }
  char buffer[64];
  effect_name(*EffectRegistry::at(this->engine_.effect_index()), buffer, sizeof(buffer));
  ESP_LOGW(TAG, "'%s' does not run on a %s output, starting on the first one that does", buffer,
           this->layout_2d_ ? "2D" : "1D");
  this->engine_.set_effect_index(first);
  this->notify_state_change();
}

bool WledFxController::set_effect_by_name(const std::string &name) {
  const int index = EffectRegistry::index_of(name.c_str());
  if (index < 0)
    return false;
  if (!this->effect_offered(static_cast<size_t>(index))) {
    /* Not an error: the build carries the effect, this output just cannot show
     * it. A select that offers only what fits never asks for this, so anything
     * arriving here is an action or an API call, and ignoring it leaves the
     * panel on what it was running. */
    ESP_LOGW(TAG, "'%s' does not run on a %s output, ignoring. %s", name.c_str(),
             this->layout_2d_ ? "2D" : "1D",
             this->layout_2d_ ? "Add 'include_1d_effects: true' to offer the 1D effects here."
                              : "Give this output a width and a height to run the 2D effects.");
    return false;
  }
  if (!this->engine_.set_effect_index(static_cast<size_t>(index)))
    return false;
  // The new effect's metadata just refilled every unpinned control.
  this->notify_state_change();
  return true;
}

bool WledFxController::set_palette_by_name(const std::string &name) {
  if (!this->engine_.set_palette_by_name(name.c_str()))
    return false;
  this->notify_state_change();
  return true;
}

void WledFxController::next_effect() {
  const size_t total = EffectRegistry::count();
  if (total == 0)
    return;
  /* Steps over the effects this output does not offer, so "next" on a matrix
   * never lands on a 1D-only effect that the select does not even list. One
   * pass at most: if nothing is offered, it stops where it started. */
  for (size_t step = 1; step <= total; step++) {
    const size_t candidate = (this->engine_.effect_index() + step) % total;
    if (!this->effect_offered(candidate))
      continue;
    this->engine_.set_effect_index(candidate);
    this->notify_state_change();
    return;
  }
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

bool WledFxController::frame_due_(uint32_t now) {
  if (!this->have_deadline_) {
    this->have_deadline_ = true;
    this->next_frame_ = now + this->frame_interval_;
    return true;  // first frame after a start, draw something straight away
  }
  // Signed comparison, so this stays right across the millis() wrap.
  if (static_cast<int32_t>(now - this->next_frame_) < 0)
    return false;
  this->next_frame_ += this->frame_interval_;
  // More than a whole period behind: the loop stalled, so start again from here
  // rather than render a burst of frames to catch up.
  if (static_cast<int32_t>(now - this->next_frame_) >= 0)
    this->next_frame_ = now + this->frame_interval_;
  return true;
}

void WledFxController::set_text_value(const std::string &text) {
  this->text_ = text;
  this->engine_.set_text(this->text_.c_str());
}

void WledFxController::set_color_slot(unsigned slot, uint32_t rgb) {
  switch (slot) {
    case 0:
      this->engine_.set_primary_color(rgb);
      break;
    case 1:
      this->engine_.set_secondary_color(rgb);
      break;
    case 2:
      this->engine_.set_tertiary_color(rgb);
      break;
    default:
      break;
  }
}

uint32_t WledFxController::color_slot(unsigned slot) const {
  return slot < 3 ? const_cast<Engine &>(this->engine_).segment().colors[slot] : 0;
}

void WledFxController::reset_profile(uint32_t now) {
  this->profile_ = ProfileStats{};
  this->profile_effect_start_ = now;
  this->profile_counting_ = false;
}

/* The first second is dropped rather than averaged in, so the figure is the
 * steady state rather than the steady state plus one allocation. */
static const uint32_t PROFILE_WARMUP_MS = 1000;

void WledFxController::profile_frame_(uint32_t now, uint32_t render_us, uint32_t output_us) {
  const size_t index = this->engine_.effect_index();
  if (index != this->profile_effect_index_) {
    this->profile_effect_index_ = index;
    this->reset_profile(now);
    return;
  }
  if (!this->profile_counting_) {
    if (now - this->profile_effect_start_ < PROFILE_WARMUP_MS)
      return;
    this->profile_counting_ = true;
    this->profile_effect_start_ = now;
    return;  // this frame is the boundary, so it starts the next window
  }

  ProfileStats &p = this->profile_;
  if (p.frames == 0) {
    p.render_min_us = p.render_max_us = render_us;
    p.output_min_us = p.output_max_us = output_us;
  } else {
    if (render_us < p.render_min_us)
      p.render_min_us = render_us;
    if (render_us > p.render_max_us)
      p.render_max_us = render_us;
    if (output_us < p.output_min_us)
      p.output_min_us = output_us;
    if (output_us > p.output_max_us)
      p.output_max_us = output_us;
  }
  p.render_total_us += render_us;
  p.output_total_us += output_us;
  p.frames++;
  p.window_ms = now - this->profile_effect_start_;
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
  this->ensure_offered_effect();
}

void WledFxDisplay::loop() {
  if (this->frame_ == nullptr)
    return;
  const uint32_t now = App.get_loop_component_start_time();
  if (!this->frame_due_(now))
    return;

  if (!this->output_enabled()) {
    /* Blank once and then leave the panel alone. The engine is not run at all,
     * so an off panel costs nothing, and the effect picks up where the clock is
     * when it comes back, which is what WLED's power button does. */
    if (!this->blanked_) {
      memset(this->frame_, 0, static_cast<size_t>(this->width_) * this->height_ * 3);
      this->push_frame_();
      this->blanked_ = true;
    }
    return;
  }
  this->blanked_ = false;

  const uint32_t render_start = micros();
  this->engine_.render(now);
  const uint32_t render_end = micros();

  const Canvas &canvas = this->engine_.canvas();
  const size_t pixels = canvas.size();
  uint8_t *out = this->frame_;
  const uint8_t brightness = this->output_brightness();
  if (brightness == 255) {
    for (size_t i = 0; i < pixels; i++) {
      const uint32_t c = canvas.get(i);
      *out++ = this->gamma_lut_[(c >> 16) & 0xFF];
      *out++ = this->gamma_lut_[(c >> 8) & 0xFF];
      *out++ = this->gamma_lut_[c & 0xFF];
    }
  } else {
    // Master brightness is a plain linear dim of the finished frame, so it is
    // applied after gamma rather than bent by it.
    for (size_t i = 0; i < pixels; i++) {
      const uint32_t c = canvas.get(i);
      *out++ = this->scale_output_(this->gamma_lut_[(c >> 16) & 0xFF]);
      *out++ = this->scale_output_(this->gamma_lut_[(c >> 8) & 0xFF]);
      *out++ = this->scale_output_(this->gamma_lut_[c & 0xFF]);
    }
  }

  this->push_frame_();
  this->profile_frame_(now, render_end - render_start, micros() - render_end);
}

void WledFxDisplay::push_frame_() {
  /* big_endian is true because the frame holds each pixel as the three bytes
   * R, G, B in that order, and that is the 24 bit value 0xRRGGBB stored most
   * significant byte first.
   *
   * It said false until a capture of a real WLED device put the two hue
   * histograms side by side and every effect came out rotated half a turn.
   * hub75 never noticed: its draw_pixels_at() hands packed 24 bit straight to
   * its driver, which reads the three bytes in memory order and ignores the
   * flag. Every display that does not override draw_pixels_at() falls back to
   * Display::draw_pixels_at(), which for 888 with big_endian false packs the
   * first byte as the LEAST significant one and hands red and blue to each
   * other. So this rendered correctly on the one panel it had been tried on
   * and swapped on every SPI display, every DisplayBuffer and the snapshot
   * display the comparison harness uses. */
  this->display_->draw_pixels_at(0, 0, this->width_, this->height_, this->frame_, display::COLOR_ORDER_RGB,
                                 display::COLOR_BITNESS_888, true);
  // Pushes the frame out. On hub75 with double buffering this is the flip.
  this->display_->update();
}

void WledFxDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "WLED FX display:\n"
                "  Canvas: %dx%d\n"
                "  Frame interval: %" PRIu32 " ms\n"
                "  Gamma: %.2f\n"
                "  Effects compiled in: %u\n"
                "  Effect: %s\n"
                "  Palette: %s",
                this->width_, this->height_, this->frame_interval(), this->gamma_,
                static_cast<unsigned>(EffectRegistry::count()), this->current_effect_name().c_str(),
                this->current_palette_name().c_str());
}

#endif  // USE_DISPLAY

}  // namespace wled_fx
}  // namespace esphome
