// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_canvas.h"

#include <cstring>

namespace esphome {
namespace wled_fx {

bool Canvas::allocate(uint16_t width, uint16_t height) {
  this->release();
  if (width == 0 || height == 0)
    return false;
  const size_t pixel_count = static_cast<size_t>(width) * height;
  const size_t words = pixel_count + 2 * CANVAS_GUARD_WORDS;
  /* The hot buffer of the whole component, so it asks for internal RAM first.
   * platform_alloc_fast() decides whether the board can spare it and says which
   * way it went. */
  this->buffer_ = static_cast<uint32_t *>(platform_alloc_fast(words * sizeof(uint32_t), &this->internal_));
  if (this->buffer_ == nullptr)
    return false;
  this->pixels_ = this->buffer_ + CANVAS_GUARD_WORDS;
  this->width_ = width;
  this->height_ = height;
  for (size_t i = 0; i < CANVAS_GUARD_WORDS; i++) {
    this->buffer_[i] = CANVAS_GUARD_PATTERN;
    this->pixels_[pixel_count + i] = CANVAS_GUARD_PATTERN;
  }
  this->clear();
  return true;
}

void Canvas::release() {
  if (this->buffer_ != nullptr) {
    platform_free(this->buffer_);
    this->buffer_ = nullptr;
    this->pixels_ = nullptr;
  }
  this->width_ = 0;
  this->height_ = 0;
  this->internal_ = false;
}

void Canvas::clear() {
  if (this->pixels_ == nullptr)
    return;
  memset(this->pixels_, 0, this->size() * sizeof(uint32_t));
}

bool Canvas::guards_intact() const {
  if (this->buffer_ == nullptr)
    return true;
  const size_t pixel_count = this->size();
  for (size_t i = 0; i < CANVAS_GUARD_WORDS; i++) {
    if (this->buffer_[i] != CANVAS_GUARD_PATTERN)
      return false;
    if (this->pixels_[pixel_count + i] != CANVAS_GUARD_PATTERN)
      return false;
  }
  return true;
}

}  // namespace wled_fx
}  // namespace esphome
