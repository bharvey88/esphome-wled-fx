/* See wf_font.cpp for the WLED attribution and licence notice. */

#include "wf_font.h"

namespace esphome {
namespace wled_fx {

bool Font::pixel(uint8_t chr, uint8_t x, uint8_t y) const {
  if (chr < this->first_char() || chr > this->last_char())
    return false;
  const uint8_t w = this->width();
  const uint8_t h = this->height();
  if (x >= w || y >= h)
    return false;
  const size_t glyph_index = static_cast<size_t>(chr) - this->first_char();
  const uint8_t *glyph = this->packed_ + 12 + glyph_index * this->glyph_stride();
  const size_t bit = static_cast<size_t>(y) * w + x;
  return (glyph[bit >> 3] >> (7 - (bit & 7))) & 1;
}

}  // namespace wled_fx
}  // namespace esphome
