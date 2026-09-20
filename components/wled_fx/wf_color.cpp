/* See wf_color.h for the combined MIT (FastLED) and EUPL-to-GPLv3 (WLED) notice
 * that covers this file. */

#include "wf_color.h"

namespace esphome {
namespace wled_fx {

/* WLED's two gamma tables at its default gamma of 2.2, the exact output of
 *
 *   gammaT[i]     = (int)(powf( i        / 255.0f, 2.2f    ) * 255.0f + 0.5f)
 *   gammaT_inv[i] = (int)(powf((i - 0.5f)/ 255.0f, 1/2.2f  ) * 255.0f + 0.5f)
 *
 * with both zero entries forced to 0 (wled00/colors.cpp:652-663). They are
 * constants rather than a boot-time calculation because nothing here can change
 * the gamma value, and 512 bytes of flash beats 512 bytes of RAM plus 510 calls
 * to powf at start-up. wled_fx_effect_test recomputes them and fails on a drift
 * of one byte, so they cannot rot. */
const uint8_t GAMMA_T[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
      3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
      6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
     12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
     20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
     30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
     42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
     56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
     73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
     91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
    113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
    137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
    163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
    192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
};

const uint8_t GAMMA_T_INV[256] = {
      0,  15,  25,  31,  36,  41,  45,  48,  51,  54,  57,  60,  62,  65,  67,  69,
     71,  73,  75,  77,  79,  81,  83,  85,  86,  88,  90,  91,  93,  94,  96,  97,
     99, 100, 101, 103, 104, 105, 107, 108, 109, 110, 112, 113, 114, 115, 116, 118,
    119, 120, 121, 122, 123, 124, 125, 126, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 148, 149,
    150, 151, 152, 153, 154, 154, 155, 156, 157, 158, 158, 159, 160, 161, 162, 162,
    163, 164, 165, 165, 166, 167, 168, 169, 169, 170, 171, 171, 172, 173, 174, 174,
    175, 176, 177, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185, 185,
    186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196,
    196, 197, 198, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205,
    206, 207, 207, 208, 208, 209, 210, 210, 211, 211, 212, 212, 213, 213, 214, 215,
    215, 216, 216, 217, 217, 218, 218, 219, 220, 220, 221, 221, 222, 222, 223, 223,
    224, 224, 225, 225, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232,
    232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 240,
    240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 245, 246, 246, 247, 247,
    248, 248, 249, 249, 250, 250, 251, 251, 252, 252, 252, 253, 253, 254, 254, 255,
};

// derived from FastLED
void hsv2rgb_rainbow(uint16_t h, uint8_t s, uint8_t v, uint8_t *rgbdata, bool is_rgbw) {
  uint8_t hue = h >> 8;
  uint8_t sat = s;
  uint32_t val = v;
  uint32_t offset = h & 0x1FFF;
  uint32_t third16 = offset * 21846;
  uint8_t third = third16 >> 21;
  uint8_t r, g, b;

  if (!(hue & 0x80)) {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) {
        r = 255 - third;
        g = third;
        b = 0;
      } else {
        r = 171;
        g = 85 + third;
        b = 0;
      }
    } else {
      if (!(hue & 0x20)) {
        uint8_t twothirds = third16 >> 20;
        r = 171 - twothirds;
        g = 170 + third;
        b = 0;
      } else {
        r = 0;
        g = 255 - third;
        b = third;
      }
    }
  } else {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) {
        r = 0;
        uint8_t twothirds = third16 >> 20;
        g = 171 - twothirds;
        b = 85 + twothirds;
      } else {
        r = third;
        g = 0;
        b = 255 - third;
      }
    } else {
      if (!(hue & 0x20)) {
        r = 85 + third;
        g = 0;
        b = 171 - third;
      } else {
        r = 170 + third;
        g = 0;
        b = 85 - third;
      }
    }
  }

  if (sat != 255) {
    if (sat == 0) {
      r = 255;
      g = 255;
      b = 255;
    } else {
      uint32_t desat = 255 - sat;
      desat = desat * desat;
      uint8_t brightness_floor = desat >> 8;
      uint32_t satscale = 0xFFFF - desat;
      if (r)
        r = (r * satscale) >> 16;
      if (g)
        g = (g * satscale) >> 16;
      if (b)
        b = (b * satscale) >> 16;
      r += brightness_floor;
      g += brightness_floor;
      b += brightness_floor;
    }
  }

  if (val != 255) {
    if (val == 0) {
      r = 0;
      g = 0;
      b = 0;
    } else {
      val = val * val + 512;
      if (r)
        r = ((r * val) >> 16) + 1;
      if (g)
        g = ((g * val) >> 16) + 1;
      if (b)
        b = ((b * val) >> 16) + 1;
    }
  }

  if (is_rgbw) {
    rgbdata[0] = b;
    rgbdata[1] = g;
    rgbdata[2] = r;
  } else {
    rgbdata[0] = r;
    rgbdata[1] = g;
    rgbdata[2] = b;
  }
}

// derived from FastLED
CRGB HeatColor(uint8_t temperature) {
  CRGB heatcolor;
  uint8_t t192 = ((static_cast<int>(temperature) * 191) >> 8) + (temperature ? 1 : 0);
  uint8_t heatramp = t192 & 0x3F;
  heatramp <<= 2;
  heatcolor.r = 255;
  heatcolor.b = 0;
  if (t192 & 0x80) {
    heatcolor.g = 255;
    heatcolor.b = heatramp;
  } else if (t192 & 0x40) {
    heatcolor.g = heatramp;
  } else {
    heatcolor.r = heatramp;
    heatcolor.g = 0;
  }
  return heatcolor;
}

// derived from FastLED
void fill_solid_RGB(CRGB *colors, uint32_t num, const CRGB &c1) {
  for (uint32_t i = 0; i < num; i++)
    colors[i] = c1;
}

// derived from FastLED
void fill_gradient_RGB(CRGB *colors, uint32_t startpos, CRGB startcolor, uint32_t endpos, CRGB endcolor) {
  if (endpos < startpos) {
    uint32_t t = endpos;
    CRGB tc = endcolor;
    endcolor = startcolor;
    endpos = startpos;
    startpos = t;
    startcolor = tc;
  }
  int rdistance = endcolor.r - startcolor.r;
  int gdistance = endcolor.g - startcolor.g;
  int bdistance = endcolor.b - startcolor.b;

  int divisor = endpos - startpos;
  divisor = divisor == 0 ? 1 : divisor;

  /* Upstream writes these three as `distance << 16`. A distance is a difference
   * between two channels, so it is negative whenever the gradient descends, and
   * shifting a negative value left is undefined before C++20. Every compiler
   * this builds with produces the multiply anyway; saying multiply is what makes
   * a UBSan build of the host simulator come out clean, and it cannot change the
   * result on a two's complement machine. */
  int rdelta = (rdistance * 65536) / divisor;
  int gdelta = (gdistance * 65536) / divisor;
  int bdelta = (bdistance * 65536) / divisor;

  int rshifted = startcolor.r << 16;
  int gshifted = startcolor.g << 16;
  int bshifted = startcolor.b << 16;

  for (uint32_t i = startpos; i <= endpos; i++) {
    colors[i] = CRGB(rshifted >> 16, gshifted >> 16, bshifted >> 16);
    rshifted += rdelta;
    gshifted += gdelta;
    bshifted += bdelta;
  }
}

void fill_gradient_RGB(CRGB *colors, uint32_t num, const CRGB &c1, const CRGB &c2) {
  fill_gradient_RGB(colors, 0, c1, num - 1, c2);
}

void fill_gradient_RGB(CRGB *colors, uint32_t num, const CRGB &c1, const CRGB &c2, const CRGB &c3) {
  uint32_t half = num / 2;
  fill_gradient_RGB(colors, 0, c1, half, c2);
  fill_gradient_RGB(colors, half, c2, num - 1, c3);
}

void fill_gradient_RGB(CRGB *colors, uint32_t num, const CRGB &c1, const CRGB &c2, const CRGB &c3, const CRGB &c4) {
  uint32_t onethird = num / 3;
  uint32_t twothirds = (num * 2) / 3;
  fill_gradient_RGB(colors, 0, c1, onethird, c2);
  fill_gradient_RGB(colors, onethird, c2, twothirds, c3);
  fill_gradient_RGB(colors, twothirds, c3, num - 1, c4);
}

// derived from FastLED
void nblendPaletteTowardPalette(CRGBPalette16 &current, CRGBPalette16 &target, uint8_t max_changes) {
  uint8_t *p1 = reinterpret_cast<uint8_t *>(current.entries);
  uint8_t *p2 = reinterpret_cast<uint8_t *>(target.entries);
  uint32_t changes = 0;
  const uint32_t total_channels = sizeof(CRGB) * 16;
  for (uint32_t i = 0; i < total_channels; ++i) {
    if (p1[i] == p2[i])
      continue;
    if (p1[i] < p2[i]) {
      ++p1[i];
      ++changes;
    }
    if (p1[i] > p2[i]) {
      --p1[i];
      ++changes;
      if (p1[i] > p2[i])
        --p1[i];
    }
    if (changes >= max_changes)
      break;
  }
}

CRGBPalette16::CRGBPalette16(const CRGB &c1) { fill_solid_RGB(&entries[0], 16, c1); }
CRGBPalette16::CRGBPalette16(const CRGB &c1, const CRGB &c2) { fill_gradient_RGB(&entries[0], 16, c1, c2); }
CRGBPalette16::CRGBPalette16(const CRGB &c1, const CRGB &c2, const CRGB &c3) {
  fill_gradient_RGB(&entries[0], 16, c1, c2, c3);
}
CRGBPalette16::CRGBPalette16(const CRGB &c1, const CRGB &c2, const CRGB &c3, const CRGB &c4) {
  fill_gradient_RGB(&entries[0], 16, c1, c2, c3, c4);
}

// derived from FastLED
CRGBPalette16 &CRGBPalette16::operator=(const uint8_t *gradient_palette) {
  const TRGBGradientPaletteEntryUnion *progent =
      reinterpret_cast<const TRGBGradientPaletteEntryUnion *>(gradient_palette);
  TRGBGradientPaletteEntryUnion u;

  int count = 0;
  do {
    u.dword = progent[count].dword;
    ++count;
  } while (u.index != 255);

  int last_slot_used = -1;

  u.dword = progent->dword;
  CRGB rgbstart(u.r, u.g, u.b);

  int indexstart = 0;
  int istart8 = 0;
  int iend8 = 0;
  while (indexstart < 255) {
    ++progent;
    u.dword = progent->dword;
    int indexend = u.index;
    CRGB rgbend(u.r, u.g, u.b);
    istart8 = indexstart / 16;
    iend8 = indexend / 16;
    if (count < 16) {
      if ((istart8 <= last_slot_used) && (last_slot_used < 15)) {
        istart8 = last_slot_used + 1;
        if (iend8 < istart8)
          iend8 = istart8;
      }
      last_slot_used = iend8;
    }
    fill_gradient_RGB(&entries[0], istart8, rgbstart, iend8, rgbend);
    indexstart = indexend;
    rgbstart = rgbend;
  }
  return *this;
}

uint32_t color_blend(uint32_t color1, uint32_t color2, uint8_t blend) {
  const uint32_t two_channel_mask = 0x00FF00FF;
  uint32_t rb1 = color1 & two_channel_mask;
  uint32_t wg1 = (color1 >> 8) & two_channel_mask;
  uint32_t rb2 = color2 & two_channel_mask;
  uint32_t wg2 = (color2 >> 8) & two_channel_mask;
  uint32_t rb3 = ((((rb1 << 8) | rb2) + (rb2 * blend) - (rb1 * blend)) >> 8) & two_channel_mask;
  uint32_t wg3 = ((((wg1 << 8) | wg2) + (wg2 * blend) - (wg1 * blend))) & ~two_channel_mask;
  return rb3 | wg3;
}

uint32_t color_add(uint32_t c1, uint32_t c2, bool preserve_cr) {
  if (c1 == BLACK)
    return c2;
  if (c2 == BLACK)
    return c1;
  const uint32_t two_channel_mask = 0x00FF00FF;
  uint32_t rb = (c1 & two_channel_mask) + (c2 & two_channel_mask);
  uint32_t wg = ((c1 >> 8) & two_channel_mask) + ((c2 >> 8) & two_channel_mask);

  if (preserve_cr) {
    uint32_t overflow = (rb | wg) & 0x01000100;
    if (overflow) {
      uint32_t r = rb >> 16;
      uint32_t b = rb & 0xFFFF;
      uint32_t w = wg >> 16;
      uint32_t g = wg & 0xFFFF;
      uint32_t maxval = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
      maxval = (w > maxval) ? w : maxval;
      const uint32_t scale = (uint32_t(255) << 8) / maxval;
      rb = ((rb * scale) >> 8) & two_channel_mask;
      wg = (wg * scale) & ~two_channel_mask;
    } else {
      wg <<= 8;
    }
  } else {
    rb |= ((rb & 0x01000100) - ((rb >> 8) & 0x00010001)) & 0x00FF00FF;
    wg |= ((wg & 0x01000100) - ((wg >> 8) & 0x00010001)) & 0x00FF00FF;
    wg <<= 8;
  }
  return rb | wg;
}

uint32_t color_fade(uint32_t c1, uint8_t amount, bool video) {
  if (c1 == BLACK || amount == 0)
    return 0;
  if (amount == 255)
    return c1;
  const uint32_t two_channel_mask = 0x00FF00FF;
  uint32_t rb = c1 & two_channel_mask;
  uint32_t wg = (c1 >> 8) & two_channel_mask;
  uint32_t rb_scaled;
  uint32_t wg_scaled;

  if (video) {
    rb_scaled = ((rb * amount + 0x007F007F) >> 8) & two_channel_mask;
    wg_scaled = (wg * amount + 0x007F007F) & ~two_channel_mask;
    uint8_t r = static_cast<uint8_t>(rb >> 16);
    uint8_t g = static_cast<uint8_t>(wg);
    uint8_t b = static_cast<uint8_t>(rb);
    uint8_t w = static_cast<uint8_t>(wg >> 16);
    uint8_t maxc = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    maxc = (maxc >> 2) + 1;
    rb_scaled |= r > maxc ? 0x00010000 : 0;
    wg_scaled |= g > maxc ? 0x00000100 : 0;
    rb_scaled |= b > maxc ? 0x00000001 : 0;
    wg_scaled |= w ? 0x01000000 : 0;
  } else {
    rb_scaled = ((rb * (amount + 1)) >> 8) & two_channel_mask;
    wg_scaled = (wg * (amount + 1)) & ~two_channel_mask;
  }

  return rb_scaled | wg_scaled;
}

// derived from FastLED
uint32_t ColorFromPalette(const CRGBPalette16 &pal, unsigned index, uint8_t brightness, TBlendType blend_type) {
  if (blend_type == LINEARBLEND_NOWRAP) {
    index = (index * 0xF0) >> 8;
  }
  unsigned hi4 = static_cast<uint8_t>(index) >> 4;
  unsigned lo4 = index & 0x0F;
  const CRGB *entry = &pal[0] + hi4;
  unsigned red1 = entry->r;
  unsigned green1 = entry->g;
  unsigned blue1 = entry->b;
  if (lo4 && blend_type != NOBLEND) {
    if (hi4 == 15)
      entry = &pal[0];
    else
      ++entry;
    unsigned f2 = lo4 << 4;
    unsigned f1 = 256 - f2;
    red1 = (red1 * f1 + static_cast<unsigned>(entry->r) * f2) >> 8;
    green1 = (green1 * f1 + static_cast<unsigned>(entry->g) * f2) >> 8;
    blue1 = (blue1 * f1 + static_cast<unsigned>(entry->b) * f2) >> 8;
  }
  if (brightness < 255) {
    uint32_t scale = brightness + 1;
    red1 = (red1 * scale) >> 8;
    green1 = (green1 * scale) >> 8;
    blue1 = (blue1 * scale) >> 8;
  }
  return RGBW32(red1, green1, blue1, 0);
}

void hsv2rgb_spectrum(const CHSV32 &hsv, CRGBW &rgb) {
  unsigned p, q, t;
  unsigned region = (static_cast<unsigned>(hsv.h) * 6) >> 16;
  unsigned remainder = (hsv.h - (region * 10923)) * 6;

  if (hsv.s == 0) {
    rgb.r = rgb.g = rgb.b = hsv.v;
    return;
  }

  p = (hsv.v * (255 - hsv.s)) >> 8;
  q = (hsv.v * (255 - ((hsv.s * remainder) >> 16))) >> 8;
  t = (hsv.v * (255 - ((hsv.s * (65535 - remainder)) >> 16))) >> 8;
  switch (region) {
    case 0:
      rgb.r = hsv.v;
      rgb.g = t;
      rgb.b = p;
      break;
    case 1:
      rgb.r = q;
      rgb.g = hsv.v;
      rgb.b = p;
      break;
    case 2:
      rgb.r = p;
      rgb.g = hsv.v;
      rgb.b = t;
      break;
    case 3:
      rgb.r = p;
      rgb.g = q;
      rgb.b = hsv.v;
      break;
    case 4:
      rgb.r = t;
      rgb.g = p;
      rgb.b = hsv.v;
      break;
    default:
      rgb.r = hsv.v;
      rgb.g = p;
      rgb.b = q;
      break;
  }
}

void hsv2rgb_spectrum(const CHSV &hsv, CRGB &rgb) {
  CHSV32 hsv32(hsv);
  CRGBW rgb32;
  rgb32.color32 = 0;
  hsv2rgb_spectrum(hsv32, rgb32);
  rgb = CRGB(rgb32.r, rgb32.g, rgb32.b);
}

void rgb2hsv(const CRGBW &rgb, CHSV32 &hsv) {
  int32_t r = rgb.r;
  int32_t g = rgb.g;
  int32_t b = rgb.b;
  uint32_t minval, maxval;
  int32_t delta;
  maxval = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
  if (maxval == 0) {
    hsv.hsv32 = 0;
    return;
  }
  minval = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
  hsv.v = maxval;
  delta = maxval - minval;
  if (delta != 0) {
    hsv.s = (255 * delta) / maxval;
    if (maxval == static_cast<uint32_t>(r))
      hsv.h = static_cast<uint16_t>((10923 * (g - b)) / delta);
    else if (maxval == static_cast<uint32_t>(g))
      hsv.h = static_cast<uint16_t>(21845 + (10923 * (b - r)) / delta);
    else
      hsv.h = static_cast<uint16_t>(43690 + (10923 * (r - g)) / delta);
  } else {
    hsv.s = 0;
    hsv.h = 0;
  }
}

CHSV rgb2hsv(const CRGB c) {
  CHSV32 hsv;
  rgb2hsv(CRGBW(c), hsv);
  return CHSV(hsv);
}

void adjust_color(CRGBW &rgb, int32_t hue_shift, int32_t sat_change, int32_t value_change) {
  if (rgb.color32 == 0 && value_change <= 0)
    return;
  CHSV32 hsv;
  rgb2hsv(rgb, hsv);
  hsv.h += (hue_shift << 8);
  int s = static_cast<int>(hsv.s) + sat_change;
  int v = static_cast<int>(hsv.v) + value_change;
  hsv.s = s < 0 ? 0 : (s > 255 ? 255 : s);
  hsv.v = v < 0 ? 0 : (v > 255 ? 255 : v);
  hsv2rgb_spectrum(hsv, rgb);
}

}  // namespace wled_fx
}  // namespace esphome
