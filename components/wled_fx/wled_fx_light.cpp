#include "wled_fx_light.h"

#ifdef USE_LIGHT

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.light";

void WledFxLightEffect::start() {
  auto *out = static_cast<light::AddressableLight *>(this->state_->get_output());
  const int size = out->size();

  int width = this->width_;
  int height = this->height_;
  if (width <= 0 && height <= 0) {
    width = size;
    height = 1;
  } else if (width <= 0) {
    width = height > 0 ? size / height : size;
  } else if (height <= 0) {
    height = width > 0 ? size / width : 1;
  }
  if (width * height > size) {
    ESP_LOGW(TAG, "width x height (%d) is larger than the light (%d pixels), clamping", width * height, size);
    height = size / (width > 0 ? width : 1);
  }
  if (width <= 0 || height <= 0) {
    ESP_LOGE(TAG, "Bad geometry %dx%d", width, height);
    return;
  }

  if (!this->engine_.canvas().is_allocated()) {
    if (!this->engine_.init(static_cast<uint16_t>(width), static_cast<uint16_t>(height))) {
      ESP_LOGE(TAG, "Canvas allocation failed for %dx%d", width, height);
      return;
    }
    this->engine_.set_text(this->text_.c_str());
  }
  this->width_ = width;
  this->height_ = height;
  this->ready_ = true;
  this->ensure_offered_effect();
  this->reset_frame_clock();
}

void WledFxLightEffect::stop() {
  light::AddressableLightEffect::stop();
  this->ready_ = false;
  /* Puts the effect back to frame zero, so turning the light on starts the
   * animation rather than resuming it mid stride, which is what WLED does on a
   * segment change. The canvas and the effect's scratch block are both kept:
   * see the note on Segment::allocate_data() for why nothing is handed back to
   * the heap after setup. */
  this->engine_.restart_effect();
}

void WledFxLightEffect::apply(light::AddressableLight &it, const Color &current_color) {
  if (!this->ready_)
    return;
  const uint32_t now = App.get_loop_component_start_time();
  if (!this->frame_due_(now))
    return;

  if (this->use_light_color_) {
    this->engine_.set_primary_color(RGBW32(current_color.r, current_color.g, current_color.b, current_color.w));
  }

  const uint32_t render_start = micros();
  this->engine_.render(now);
  const uint32_t render_end = micros();

  const Canvas &canvas = this->engine_.canvas();
  const int32_t size = it.size();
  const uint8_t brightness = this->output_brightness();
  for (int y = 0; y < this->height_; y++) {
    const bool flip = this->serpentine_ && (y & 1);
    for (int x = 0; x < this->width_; x++) {
      uint32_t c = canvas.get(static_cast<size_t>(x) + static_cast<size_t>(y) * this->width_);
      const int column = flip ? (this->width_ - 1 - x) : x;
      const int32_t index = y * this->width_ + column;
      if (index >= size)
        continue;
      if (brightness != 255) {
        c = RGBW32(this->scale_output_((c >> 16) & 0xFF), this->scale_output_((c >> 8) & 0xFF),
                   this->scale_output_(c & 0xFF), this->scale_output_((c >> 24) & 0xFF));
      }
      it[index].set_rgbw((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
    }
  }
  it.schedule_show();
  this->profile_frame_(now, render_end - render_start, micros() - render_end);
}

}  // namespace wled_fx
}  // namespace esphome

#endif  // USE_LIGHT
