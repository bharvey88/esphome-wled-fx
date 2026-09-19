/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * 2D framework (c) 2022 Blaz Kristan (@blazoncek).
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

#include "wf_effects.h"

#define WLED_FX_GROUP_2D_A \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MATRIX || WLED_FX_FX_METABALLS || WLED_FX_FX_SCROLLING_TEXT)

#if WLED_FX_GROUP_2D_A

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MATRIX
///////////////////////
//    2D Matrix      //
///////////////////////
// Matrix2D. By Jeremy Williams. Adapted by Andrew Tuline & improved by merkisoft
// and ewowi, and softhack007.
void mode_2Dmatrix(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  const auto XY = [&](int x, int y) { return (x % cols) + (y % rows) * cols; };

  unsigned dataSize = (static_cast<unsigned>(cols * rows) + 7) >> 3;  // 1 bit per LED for trails
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed

  if (seg.call == 0) {
    seg.fill(BLACK);
    seg.step = 0;
  }

  uint8_t fade = wf_map(seg.custom1, 0, 255, 30, 250);  // equals trail size
  uint8_t speed = (256 - seg.speed) >> wf_map(rows < 150 ? rows : 150, 0, 150, 0, 3);  // slower for small displays

  uint32_t spawnColor;
  uint32_t trailColor;
  if (seg.check1) {
    spawnColor = seg.color(0);
    trailColor = seg.color(1);
  } else {
    spawnColor = RGBW32(gamma8inv(175), gamma8inv(255), gamma8inv(175), 0);
    trailColor = RGBW32(gamma8inv(27), gamma8inv(130), gamma8inv(39), 0);
  }

  bool emptyScreen = true;
  if (seg.now - seg.step >= speed) {
    seg.step = seg.now;
    // move pixels one row down. Falling codes keep color and add trail pixels; all
    // other pixels are faded
    seg.fade_to_black_by(fade);
    for (int row = rows - 1; row >= 0; row--) {
      for (int col = 0; col < cols; col++) {
        unsigned index = XY(col, row) >> 3;
        unsigned bitNum = XY(col, row) & 0x07;
        if (seg.data[index] & (1 << bitNum)) {
          seg.set_pixel_color_xy(col, row, trailColor);  // create trail
          seg.data[index] &= ~(1 << bitNum);
          if (row < rows - 1) {
            seg.set_pixel_color_xy(col, row + 1, spawnColor);
            index = XY(col, row + 1) >> 3;
            bitNum = XY(col, row + 1) & 0x07;
            seg.data[index] |= (1 << bitNum);
            emptyScreen = false;
          }
        }
      }
    }

    // spawn new falling code
    if (hw_random8() <= seg.intensity || emptyScreen) {
      uint8_t spawnX = hw_random8(cols);
      seg.set_pixel_color_xy(spawnX, 0, spawnColor);
      unsigned index = XY(spawnX, 0) >> 3;
      unsigned bitNum = XY(spawnX, 0) & 0x07;
      seg.data[index] |= (1 << bitNum);
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METABALLS
/////////////////////////
//     2D Metaballs    //
/////////////////////////
// Metaballs by Stefan Petrick. Cannot have one of the dimensions be 2 or less.
// Adapted by Andrew Tuline.
void mode_2Dmetaballs(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  float speed = 0.25f * (1 + (seg.speed >> 6));

  // get some 2 random moving points
  int x2 = wf_map(perlin8(seg.now * speed, 25355, 685), 0, 255, 0, cols - 1);
  int y2 = wf_map(perlin8(seg.now * speed, 355, 11685), 0, 255, 0, rows - 1);

  int x3 = wf_map(perlin8(seg.now * speed, 55355, 6685), 0, 255, 0, cols - 1);
  int y3 = wf_map(perlin8(seg.now * speed, 25355, 22685), 0, 255, 0, rows - 1);

  // and one Lissajou function
  int x1 = beatsin8_t(23 * speed, 0, cols - 1, seg.now);
  int y1 = beatsin8_t(28 * speed, 0, rows - 1, seg.now);

  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      // calculate distances of the 3 points from actual pixel
      // and add them together with weightening
      unsigned dx = abs(x - x1);
      unsigned dy = abs(y - y1);
      unsigned dist = 2 * sqrt32_bw((dx * dx) + (dy * dy));

      dx = abs(x - x2);
      dy = abs(y - y2);
      dist += sqrt32_bw((dx * dx) + (dy * dy));

      dx = abs(x - x3);
      dy = abs(y - y3);
      dist += sqrt32_bw((dx * dx) + (dy * dy));

      // inverse result
      int color = dist ? 1000 / dist : 255;

      // map color between thresholds
      if (color > 0 && color < 60) {
        seg.set_pixel_color_xy(
            x, y, seg.color_from_palette(wf_map(color * 9, 9, 531, 0, 255), false, seg.palette_solid_wrap(), 0));
      } else {
        seg.set_pixel_color_xy(x, y, seg.color_from_palette(0, false, seg.palette_solid_wrap(), 0));
      }
      // show the 3 points, too
      seg.set_pixel_color_xy(x1, y1, WHITE);
      seg.set_pixel_color_xy(x2, y2, WHITE);
      seg.set_pixel_color_xy(x3, y3, WHITE);
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCROLLING_TEXT
////////////////////////////
//     2D Scrolling text  //
////////////////////////////
// Text comes from seg.text. The upstream effect also renders live date and time
// tokens and loads custom fonts from the filesystem; see the deviations section of
// PORTING.md.
void mode_2Dscrollingtext(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  const char *text = seg.text != nullptr ? seg.text : "";
  const int numberOfChars = static_cast<int>(strlen(text));
  if (numberOfChars == 0) {
    seg.fade_out(255 - (seg.custom1 >> 4));
    return;
  }

  // Font selection
  uint8_t fontNum = wf_map(seg.custom2, 0, 255, 0, static_cast<long>(FONT_COUNT) - 1);
  const Font &font = FONTS[fontNum];

  // letters orientation: -2/+2 = upside down, -1 = 90 clockwise, 0 = normal,
  // 1 = 90 counterclockwise
  const int8_t rotate = wf_map(seg.custom3, 0, 31, -2, 2);
  const bool isRotated = (rotate == 1 || rotate == -1);

  const int fontHeight = font.height();
  const int fontWidth = font.width();
  const int letterSpacing = isRotated ? 1 : font.spacing();

  const int glyphWidth = isRotated ? fontHeight : fontWidth;
  const int glyphHeight = isRotated ? fontWidth : fontHeight;
  int totalTextWidth = numberOfChars * (glyphWidth + letterSpacing) - letterSpacing;

  // y-offset calculation
  int yoffset = wf_map(seg.intensity, 0, 255, -rows / 2, rows / 2);

  if (totalTextWidth <= cols) {
    // if text fits matrix width, scroll vertically
    int speed = wf_map(seg.speed, 0, 255, 5000, 1000);
    int frac = seg.now % speed + 1;
    if (seg.intensity == 255) {
      yoffset = (2 * frac * rows) / speed - rows;
    } else if (seg.intensity == 0) {
      yoffset = rows - (2 * frac * rows) / speed;
    }
  }

  // scroll step (aux0 is current scrolling offset)
  if (seg.step < seg.now) {
    if (totalTextWidth > cols) {
      if (seg.check3) {  // reverse direction
        if (seg.aux0 == 0)
          seg.aux0 = totalTextWidth + cols - 1;
        else
          --seg.aux0;
      } else {
        ++seg.aux0 %= totalTextWidth + cols;
      }
    } else {
      seg.aux0 = (cols + totalTextWidth) / 2;  // text fits, position it at the center
    }
    ++seg.aux1 &= 0xFF;  // color shift
    seg.step = seg.now + wf_map(seg.speed, 0, 255, 250, 50);
  }

  seg.fade_out(255 - (seg.custom1 >> 4));  // trail
  uint32_t col1 = seg.color_from_palette(seg.aux1, false, seg.palette_solid_wrap(), 0);
  uint32_t col2 = BLACK;

  // if gradient is selected and palette is default (0) the character is painted
  // with a gradient from color 0 to color 2; otherwise the palette colour is used
  if (seg.check1) {  // use gradient
    if (seg.palette == 0) {
      col1 = seg.color(0);
      col2 = seg.color(2);
    }
  } else {
    col2 = col1;  // force characters to use a single color
  }

  // Draw characters
  int currentXOffset = 0;
  for (int c = 0; c < numberOfChars; c++) {
    int drawX = cols - static_cast<int>(seg.aux0) + currentXOffset;
    if (drawX >= cols)
      break;  // skip if character is off-screen on the right
    const int advance = glyphWidth + letterSpacing;
    if (drawX + advance < 0) {
      currentXOffset += advance;
      continue;  // skip if off-screen on the left
    }
    const int drawY = yoffset + (rows - glyphHeight) / 2;  // center glyph vertically
    seg.draw_character(font, static_cast<uint8_t>(text[c]), drawX, drawY, col1, col2, rotate);
    currentXOffset += advance;
  }
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_MATRIX
    {"Matrix@!,Spawning rate,Trail,,,Custom color;Spawn,Trail;;2", mode_2Dmatrix},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_METABALLS
    {"Metaballs@!;;!;2", mode_2Dmetaballs},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SCROLLING_TEXT
    {"Scrolling Text@!,Y Offset,Trail,Font size,Rotate,Gradient,,Reverse;!,!,Gradient;!;2;ix=128,c1=0",
     mode_2Dscrollingtext},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table below would not be able to see it.
extern const EffectGroup EFFECT_GROUP_2D_A;
const EffectGroup EFFECT_GROUP_2D_A{"2d_a", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_2D_A
