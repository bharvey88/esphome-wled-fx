#pragma once

/* Fixed-width bitmap font access for the text effects. The five fonts are WLED
 * 16.0.1's console fonts in their packed WBF layout; see wf_font.cpp for the
 * licence and packing notes. */

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace wled_fx {

// A packed WBF font. Only the fixed-width variant is supported; every font
// shipped here has the variable-width flag clear.
class Font {
 public:
  explicit constexpr Font(const uint8_t *packed) : packed_(packed) {}

  uint8_t height() const { return this->packed_[1]; }
  uint8_t width() const { return this->packed_[2]; }
  uint8_t spacing() const { return this->packed_[3]; }
  uint8_t first_char() const { return this->packed_[5]; }
  uint8_t last_char() const { return this->packed_[6]; }

  // Bytes each glyph occupies, rows packed MSB first and padded to a byte.
  size_t glyph_stride() const { return (static_cast<size_t>(this->width()) * this->height() + 7) / 8; }

  // True when the glyph pixel at (x, y) is set. Characters outside the font range
  // render as blank.
  bool pixel(uint8_t chr, uint8_t x, uint8_t y) const;

 protected:
  const uint8_t *packed_;
};

extern const Font FONTS[];
extern const size_t FONT_COUNT;

}  // namespace wled_fx
}  // namespace esphome
