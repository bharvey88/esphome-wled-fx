/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * 2D framework (c) 2022 Blaz Kristan (@blazoncek).
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

/* 2D only, non-audio, non-particle. See BATCHES.md. */

#include "wf_effects.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_2D_B \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLACK_HOLE || WLED_FX_FX_COLORED_BURSTS || WLED_FX_FX_DNA || \
   WLED_FX_FX_DNA_SPIRAL || WLED_FX_FX_DRIFT || WLED_FX_FX_FIRENOISE || WLED_FX_FX_FRIZZLES || \
   WLED_FX_FX_GAME_OF_LIFE || WLED_FX_FX_HIPHOTIC || WLED_FX_FX_JULIA || WLED_FX_FX_LISSAJOUS || \
   WLED_FX_FX_NOISE2D || WLED_FX_FX_PLASMA_BALL || WLED_FX_FX_POLAR_LIGHTS || WLED_FX_FX_PULSER || \
   WLED_FX_FX_SINDOTS || WLED_FX_FX_SQUARED_SWIRL || WLED_FX_FX_SUN_RADIATION || WLED_FX_FX_TARTAN || \
   WLED_FX_FX_SPACESHIPS || WLED_FX_FX_CRAZY_BEES || WLED_FX_FX_GHOST_RIDER || WLED_FX_FX_BLOBS || \
   WLED_FX_FX_DRIFT_ROSE || WLED_FX_FX_ROTOZOOMER || WLED_FX_FX_DISTORTION_WAVES || WLED_FX_FX_SOAP || \
   WLED_FX_FX_OCTOPUS || WLED_FX_FX_WAVING_CELL)

#if WLED_FX_GROUP_2D_B

namespace esphome {
namespace wled_fx {
namespace {

// DARKSLATEGRAY and the shared PRNG are engine code now, in wf_color.h and
// wf_fx_shared.h.
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLACK_HOLE
// Black hole
void mode_2DBlackHole(Segment &seg) {  // By: Stepko https://editor.soulmatelights.com/gallery/1012 , Modified by:
                                       // Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  int x, y;

  seg.fade_to_black_by(16 + (seg.speed >> 3));  // create fading trails
  unsigned long t = seg.now / 128;              // timebase
  // outer stars
  for (size_t i = 0; i < 8; i++) {
    x = beatsin8_t(seg.custom1 >> 3, 0, cols - 1, seg.now, 0, ((i % 2) ? 128 : 0) + t * i);
    y = beatsin8_t(seg.intensity >> 3, 0, rows - 1, seg.now, 0, ((i % 2) ? 192 : 64) + t * i);
    seg.add_pixel_color_xy(x, y, seg.color_from_palette(i * 32, false, seg.palette_solid_wrap(), seg.check1 ? 0 : 255));
  }
  // inner stars
  for (size_t i = 0; i < 4; i++) {
    x = beatsin8_t(seg.custom2 >> 3, cols / 4, cols - 1 - cols / 4, seg.now, 0, ((i % 2) ? 128 : 0) + t * i);
    y = beatsin8_t(seg.custom3, rows / 4, rows - 1 - rows / 4, seg.now, 0, ((i % 2) ? 192 : 64) + t * i);
    seg.add_pixel_color_xy(x, y,
                           seg.color_from_palette(255 - i * 64, false, seg.palette_solid_wrap(), seg.check1 ? 0 : 255));
  }
  // central white dot
  seg.set_pixel_color_xy(cols / 2, rows / 2, WHITE);
  // blur everything a bit
  if (seg.check3)
    seg.blur(16, cols * rows < 100);
}  // mode_2DBlackHole()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORED_BURSTS
////////////////////////////
//     2D Colored Bursts  //
////////////////////////////
void mode_2DColoredBursts(Segment &seg) {  // By: ldirko
                                           // https://editor.soulmatelights.com/gallery/819-colored-bursts , modified
                                           // by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.aux0 = 0;  // start with red hue
  }

  bool dot = seg.check3;
  bool grad = seg.check1;

  uint8_t numLines = seg.intensity / 16 + 1;

  seg.aux0++;  // hue
  seg.fade_to_black_by(40 - seg.check2 * 8);
  for (size_t i = 0; i < numLines; i++) {
    uint8_t x1 = beatsin8_t(2 + seg.speed / 16, 0, (cols - 1), seg.now);
    uint8_t x2 = beatsin8_t(1 + seg.speed / 16, 0, (rows - 1), seg.now);
    uint8_t y1 = beatsin8_t(5 + seg.speed / 16, 0, (cols - 1), seg.now, 0, i * 24);
    uint8_t y2 = beatsin8_t(3 + seg.speed / 16, 0, (rows - 1), seg.now, 0, i * 48 + 64);
    uint32_t color = ColorFromPalette(seg.palette_ref(), i * 255 / numLines + (seg.aux0 & 0xFF), 255, LINEARBLEND);

    uint8_t xsteps = abs8(x1 - y1) + 1;
    uint8_t ysteps = abs8(x2 - y2) + 1;
    uint8_t steps = xsteps >= ysteps ? xsteps : ysteps;
    // Draw gradient line
    for (size_t j = 1; j <= steps; j++) {
      uint8_t rate = j * 255 / steps;
      uint8_t dx = lerp8by8(x1, y1, rate);
      uint8_t dy = lerp8by8(x2, y2, rate);
      // seg.set_pixel_color_xy(dx, dy, grad ? color_fade(color, (255-rate), true) : color); // use add_pixel_color_xy
      // for different look
      seg.add_pixel_color_xy(dx, dy, color);  // use set_pixel_color_xy for different look
      if (grad)
        seg.fade_pixel_color_xy(dx, dy, rate);
    }

    if (dot) {  // add white point at the ends of line
      seg.set_pixel_color_xy(x1, x2, WHITE);
      seg.set_pixel_color_xy(y1, y2, DARKSLATEGRAY);
    }
  }
  seg.blur(seg.custom3 >> 1, seg.check2);
}  // mode_2DColoredBursts()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DNA
/////////////////////
//      2D DNA     //
/////////////////////
void mode_2Ddna(Segment &seg) {  // dna originally by by ldirko at https://pastebin.com/pCkkkzcs. Updated by Preyy.
                                 // WLED conversion by Andrew Tuline.
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  seg.fade_to_black_by(64);
  for (int i = 0; i < cols; i++) {
    seg.set_pixel_color_xy(i, beatsin8_t(seg.speed / 8, 0, rows - 1, seg.now, 0, i * 4),
                           ColorFromPalette(seg.palette_ref(), i * 5 + seg.now / 17,
                                            beatsin8_t(5, 55, 255, seg.now, 0, i * 10), LINEARBLEND));
    seg.set_pixel_color_xy(i, beatsin8_t(seg.speed / 8, 0, rows - 1, seg.now, 0, i * 4 + 128),
                           ColorFromPalette(seg.palette_ref(), i * 5 + 128 + seg.now / 17,
                                            beatsin8_t(5, 55, 255, seg.now, 0, i * 10 + 128), LINEARBLEND));
  }
  seg.blur(seg.intensity / (8 - (seg.check1 * 2)), seg.check1);
}  // mode_2Ddna()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DNA_SPIRAL
/////////////////////////
//     2D DNA Spiral   //
/////////////////////////
void mode_2DDNASpiral(Segment &seg) {  // By: ldirko
                                       // https://editor.soulmatelights.com/gallery/512-dna-spiral-variation ,
                                       // modified by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  unsigned speeds = seg.speed / 2 + 7;
  unsigned freq = seg.intensity / 8;

  uint32_t ms = seg.now / 20;
  seg.fade_to_black_by(135);

  for (int i = 0; i < rows; i++) {
    int x = beatsin8_t(speeds, 0, cols - 1, seg.now, 0, i * freq) +
            beatsin8_t(speeds - 7, 0, cols - 1, seg.now, 0, i * freq + 128);
    int x1 = beatsin8_t(speeds, 0, cols - 1, seg.now, 0, 128 + i * freq) +
             beatsin8_t(speeds - 7, 0, cols - 1, seg.now, 0, 128 + 64 + i * freq);
    unsigned hue = (i * 128 / rows) + ms;
    // skip every 4th row every now and then (fade it more)
    if ((i + ms / 8) & 3) {
      // draw a gradient line between x and x1
      x = x / 2;
      x1 = x1 / 2;
      /* Upstream writes abs8() here, which narrows to int8_t. x and x1 each run
       * to cols - 1, so on a panel 129 or more pixels wide their difference can
       * be exactly -128, abs8(-128) is -128 again, and `unsigned steps` becomes
       * 4294967169: the loop below runs for four billion iterations and the
       * watchdog fires. WLED never sees it because its matrices are narrower.
       * Below 129 columns abs() and abs8() give the same answer for every input
       * this can produce, so this is the same effect everywhere upstream runs
       * and a working one everywhere else. See PORTING.md. */
      unsigned steps = static_cast<unsigned>(std::abs(x - x1)) + 1;
      bool positive = (x1 >= x);  // direction of drawing
      for (size_t k = 1; k <= steps; k++) {
        unsigned rate = k * 255 / steps;
        // unsigned dx = lerp8by8(x, x1, rate);
        unsigned dx = positive ? (x + k - 1) : (x - k + 1);  // behaves the same as "lerp8by8" but does not create holes
        // seg.set_pixel_color_xy(dx, i, ColorFromPalette(seg.palette_ref(), hue, 255,
        // LINEARBLEND).nscale8_video(rate));
        seg.add_pixel_color_xy(dx, i,
                               ColorFromPalette(seg.palette_ref(), hue, 255,
                                                LINEARBLEND));  // use set_pixel_color_xy for different look
        seg.fade_pixel_color_xy(dx, i, rate);
      }
      seg.set_pixel_color_xy(x, i, DARKSLATEGRAY);
      seg.set_pixel_color_xy(x1, i, WHITE);
    }
  }
  seg.blur(((uint16_t) seg.custom1 * 3) / (6 + seg.check1), seg.check1);
}  // mode_2DDNASpiral()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIFT
/////////////////////////
//     2D Drift        //
/////////////////////////
void mode_2DDrift(Segment &seg) {  // By: Stepko   https://editor.soulmatelights.com/gallery/884-drift , Modified by:
                                   // Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  const int colsCenter = (cols >> 1) + (cols % 2);
  const int rowsCenter = (rows >> 1) + (rows % 2);

  seg.fade_to_black_by(128);
  const float maxDim = std::max(cols, rows) / 2;
  unsigned long t = seg.now / (32 - (seg.speed >> 3));
  unsigned long t_20 = t / 20;  // softhack007: pre-calculating this gives about 10% speedup
  for (float i = 1.0f; i < maxDim; i += 0.25f) {
    float angle = radians(t * (maxDim - i));
    int mySin = sin_t(angle) * i;
    int myCos = cos_t(angle) * i;
    seg.set_pixel_color_xy(colsCenter + mySin, rowsCenter + myCos,
                           ColorFromPalette(seg.palette_ref(), (i * 20) + t_20, 255, LINEARBLEND));
    if (seg.check1)
      seg.set_pixel_color_xy(colsCenter + myCos, rowsCenter + mySin,
                             ColorFromPalette(seg.palette_ref(), (i * 20) + t_20, 255, LINEARBLEND));
  }
  seg.blur(seg.intensity >> (3 - seg.check2), seg.check2);
}  // mode_2DDrift()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRENOISE
//////////////////////////
//     2D Firenoise     //
//////////////////////////
void mode_2Dfirenoise(Segment &seg) {  // firenoise2d. By Andrew Tuline. Yet another short routine.
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  unsigned xscale = seg.intensity * 4;
  unsigned yscale = seg.speed * 8;
  unsigned indexx = 0;

  CRGBPalette16 pal = seg.check1 ? seg.palette_ref()
                                 : CRGBPalette16(CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Red,
                                                 CRGB::Red, CRGB::Red, CRGB::DarkOrange, CRGB::DarkOrange,
                                                 CRGB::DarkOrange, CRGB::Orange, CRGB::Orange, CRGB::Yellow,
                                                 CRGB::Orange, CRGB::Yellow, CRGB::Yellow);
  for (int j = 0; j < cols; j++) {
    for (int i = 0; i < rows; i++) {
      indexx = perlin8(j * yscale * rows / 255, i * xscale + seg.now / 4);  // We're moving along our Perlin map.
      seg.set_pixel_color_xy(
          j, i,
          ColorFromPalette(pal, std::min(i * indexx / 11, 225U), i * 255 / rows,
                           LINEARBLEND));  // With that value, look up the 8 bit colour palette value and assign it to
                                           // the current LED.
    }  // for i
  }  // for j
}  // mode_2Dfirenoise()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FRIZZLES
//////////////////////////////
//     2D Frizzles          //
//////////////////////////////
void mode_2DFrizzles(Segment &seg) {  // By: Stepko https://editor.soulmatelights.com/gallery/640-color-frizzles ,
                                      // Modified by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  seg.fade_to_black_by(16 + seg.check1 * 10);
  for (size_t i = 8; i > 0; i--) {
    seg.add_pixel_color_xy(beatsin8_t(seg.speed / 8 + i, 0, cols - 1, seg.now),
                           beatsin8_t(seg.intensity / 8 - i, 0, rows - 1, seg.now),
                           ColorFromPalette(seg.palette_ref(), beatsin8_t(12, 0, 255, seg.now), 255, LINEARBLEND));
  }
  seg.blur(seg.custom1 >> (3 + seg.check1), seg.check1);
}  // mode_2DFrizzles()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GAME_OF_LIFE
///////////////////////////////////////////
//   2D Cellular Automata Game of life   //
///////////////////////////////////////////
typedef struct Cell {
  uint8_t alive : 1, faded : 1, toggleStatus : 1, edgeCell : 1, oscillatorCheck : 1, spaceshipCheck : 1, unused : 2;
} Cell;

void mode_2Dgameoflife(Segment &seg) {  // Written by Ewoud Wijma, inspired by
                                        // https://natureofcode.com/book/chapter-7-cellular-automata/
                                        // and https://github.com/DougHaber/nlife-color , Modified By: Brandon Butler
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up
  const int cols = seg.width(), rows = seg.height();
  const unsigned maxIndex = cols * rows;

  if (!seg.allocate_data(seg.length() * sizeof(Cell)))
    FX_FALLBACK_STATIC;  // allocation failed

  Cell *cells = reinterpret_cast<Cell *>(seg.data);

  uint16_t &generation = seg.aux0, &gliderLength = seg.aux1;  // rename aux variables for clarity
  bool mutate = seg.check3;
  uint8_t blur = wf_map(seg.custom1, 0, 255, 255, 4);

  uint32_t bgColor = seg.color(1);
  uint32_t birthColor = seg.color_from_palette(128, false, seg.palette_solid_wrap(), 255);

  bool setup = seg.call == 0;
  if (setup) {
    // Calculate glider length LCM(rows,cols)*4 once
    unsigned a = rows, b = cols;
    while (b) {
      unsigned t = b;
      b = a % b;
      a = t;
    }
    gliderLength = (cols * rows / a) << 2;
  }

  if (abs(long(seg.now) - long(seg.step)) > 2000)
    seg.step = 0;  // Timebase jump fix
  bool paused = seg.step > seg.now;

  // Setup New Game of Life
  if ((!paused && generation == 0) || setup) {
    seg.step = seg.now + 1280;  // show initial state for 1.28 seconds
    generation = 1;
    paused = true;
    // Setup Grid
    memset(cells, 0, maxIndex * sizeof(Cell));

    for (unsigned i = 0; i < maxIndex; i++) {
      bool isAlive = !hw_random8(3);  // ~33%
      cells[i].alive = isAlive;
      cells[i].faded = !isAlive;
      unsigned x = i % cols, y = i / cols;
      cells[i].edgeCell = (x == 0 || x == unsigned(cols) - 1 || y == 0 || y == unsigned(rows) - 1);

      seg.set_pixel_color(i, isAlive ? seg.color_from_palette(hw_random8(), false, seg.palette_solid_wrap(), 0)
                                     : bgColor);
    }
  }

  // The cast is the only change from upstream: wf_map returns long, and on the
  // host long is 64 bit, so without it the comparison changes signedness.
  if (paused || (seg.now - seg.step < unsigned(1000 / wf_map(seg.speed, 0, 255, 1, 42)))) {
    // Redraw if paused or between updates to remove blur
    for (unsigned i = maxIndex; i--;) {
      if (!cells[i].alive) {
        uint32_t cellColor = seg.get_pixel_color(i);
        if (cellColor != bgColor) {
          uint32_t newColor;
          bool needsColor = false;
          if (cells[i].faded) {
            newColor = bgColor;
            needsColor = true;
          } else {
            uint32_t blended = color_blend(cellColor, bgColor, 2);
            if (blended == cellColor) {
              blended = bgColor;
              cells[i].faded = 1;
            }
            newColor = blended;
            needsColor = true;
          }
          if (needsColor)
            seg.set_pixel_color(i, newColor);
        }
      }
    }
    return;
  }

  // Repeat detection
  bool updateOscillator = generation % 16 == 0;
  bool updateSpaceship = gliderLength && generation % gliderLength == 0;
  bool repeatingOscillator = true, repeatingSpaceship = true, emptyGrid = true;

  unsigned cIndex = maxIndex - 1;
  for (unsigned y = rows; y--;)
    for (unsigned x = cols; x--; cIndex--) {
      Cell &cell = cells[cIndex];

      if (cell.alive)
        emptyGrid = false;
      if (cell.oscillatorCheck != cell.alive)
        repeatingOscillator = false;
      if (cell.spaceshipCheck != cell.alive)
        repeatingSpaceship = false;
      if (updateOscillator)
        cell.oscillatorCheck = cell.alive;
      if (updateSpaceship)
        cell.spaceshipCheck = cell.alive;

      unsigned neighbors = 0, aliveParents = 0, parentIdx[3];
      // Count alive neighbors
      for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
          if (i || j) {
            int nX = x + j, nY = y + i;
            if (cell.edgeCell) {
              nX = (nX + cols) % cols;
              nY = (nY + rows) % rows;
            }
            unsigned nIndex = nX + nY * cols;
            Cell &neighbor = cells[nIndex];
            if (neighbor.alive) {
              neighbors++;
              if (!neighbor.toggleStatus && neighbors < 4) {  // Alive and not dying
                parentIdx[aliveParents++] = nIndex;
              }
            }
          }

      uint32_t newColor;
      bool needsColor = false;

      if (cell.alive && (neighbors < 2 || neighbors > 3)) {  // Loneliness or Overpopulation
        cell.toggleStatus = 1;
        if (blur == 255)
          cell.faded = 1;
        newColor = cell.faded ? bgColor : color_blend(seg.get_pixel_color(cIndex), bgColor, blur);
        needsColor = true;
      } else if (!cell.alive) {
        uint8_t mutationRoll = mutate ? hw_random8(128) : 1;  // if 0: 3 neighbor births fail and 2 neighbor births
                                                              // mutate
        if ((neighbors == 3 && mutationRoll) ||
            (mutate && neighbors == 2 && !mutationRoll)) {  // Reproduction or Mutation
          cell.toggleStatus = 1;
          cell.faded = 0;

          if (aliveParents) {
            // Set color based on random neighbor
            unsigned parentIndex = parentIdx[hw_random8(aliveParents)];
            birthColor = seg.get_pixel_color(parentIndex);
          }
          newColor = birthColor;
          needsColor = true;
        } else if (!cell.faded) {  // No change, fade dead cells
          uint32_t cellColor = seg.get_pixel_color(cIndex);
          uint32_t blended = color_blend(cellColor, bgColor, blur);
          if (blended == cellColor) {
            blended = bgColor;
            cell.faded = 1;
          }
          newColor = blended;
          needsColor = true;
        }
      }

      if (needsColor)
        seg.set_pixel_color(cIndex, newColor);
    }
  // Loop through cells, if toggle, swap alive status
  for (unsigned i = maxIndex; i--;) {
    cells[i].alive ^= cells[i].toggleStatus;
    cells[i].toggleStatus = 0;
  }

  if (repeatingOscillator || repeatingSpaceship || emptyGrid) {
    generation = 0;    // reset on next call
    seg.step += 1024;  // pause final generation for ~1 second
  } else {
    ++generation;
    seg.step = seg.now;
  }
}  // mode_2Dgameoflife()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HIPHOTIC
/////////////////////////
//     2D Hiphotic     //
/////////////////////////
void mode_2DHiphotic(Segment &seg) {  //  By: ldirko  https://editor.soulmatelights.com/gallery/810 , Modified by:
                                      //  Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  const uint32_t a = seg.now / ((seg.custom3 >> 1) + 1);

  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      seg.set_pixel_color_xy(
          x, y,
          seg.color_from_palette(sin8_t(cos8_t(x * seg.speed / 16 + a / 3) + sin8_t(y * seg.intensity / 16 + a / 4) + a),
                                 false, seg.palette_solid_wrap(), 0));
    }
  }
}  // mode_2DHiphotic()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JULIA
/////////////////////////
//     2D Julia        //
/////////////////////////
// Sliders are:
// intensity = Maximum number of iterations per pixel.
// Custom1 = Location of X centerpoint
// Custom2 = Location of Y centerpoint
// Custom3 = Size of the area (small value = smaller area)
typedef struct Julia {
  float xcen;
  float ycen;
  float xymag;
} julia;

void mode_2DJulia(Segment &seg) {  // An animated Julia set by Andrew Tuline.
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (!seg.allocate_data(sizeof(julia)))
    FX_FALLBACK_STATIC;
  Julia *julias = reinterpret_cast<Julia *>(seg.data);

  float reAl;
  float imAg;

  if (seg.call == 0) {  // Reset the center if we've just re-started this animation.
    julias->xcen = 0.;
    julias->ycen = 0.;
    julias->xymag = 1.0;

    seg.custom1 = 128;  // Make sure the location widgets are centered to start.
    seg.custom2 = 128;
    seg.custom3 = 16;
    seg.intensity = 24;
  }

  julias->xcen = julias->xcen + (float) (seg.custom1 - 128) / 100000.f;
  julias->ycen = julias->ycen + (float) (seg.custom2 - 128) / 100000.f;
  julias->xymag = julias->xymag + (float) ((seg.custom3 - 16) << 3) / 100000.f;  // reduced resolution slider
  if (julias->xymag < 0.01f)
    julias->xymag = 0.01f;
  if (julias->xymag > 1.0f)
    julias->xymag = 1.0f;

  float xmin = julias->xcen - julias->xymag;
  float xmax = julias->xcen + julias->xymag;
  float ymin = julias->ycen - julias->xymag;
  float ymax = julias->ycen + julias->xymag;

  // Whole set should be within -1.2,1.2 to -.8 to 1.
  xmin = constrain(xmin, -1.2f, 1.2f);
  xmax = constrain(xmax, -1.2f, 1.2f);
  ymin = constrain(ymin, -0.8f, 1.0f);
  ymax = constrain(ymax, -0.8f, 1.0f);

  float dx;  // Delta x is mapped to the matrix size.
  float dy;  // Delta y is mapped to the matrix size.

  int maxIterations = 15;  // How many iterations per pixel before we give up. Make it 8 bits to match our range of
                           // colours.
  float maxCalc = 16.0;    // How big is each calculation allowed to be before we give up.

  maxIterations = seg.intensity / 2;

  // Resize section on the fly for some animaton.
  reAl = -0.94299f;  // PixelBlaze example
  imAg = 0.3162f;

  reAl += (float) sin16_t(seg.now * 34) / 655340.f;
  imAg += (float) sin16_t(seg.now * 26) / 655340.f;

  dx = (xmax - xmin) / (cols);  // Scale the delta x and y values to our matrix size.
  dy = (ymax - ymin) / (rows);

  // Start y
  float y = ymin;
  for (int j = 0; j < rows; j++) {
    // Start x
    float x = xmin;
    for (int i = 0; i < cols; i++) {
      // Now we test, as we iterate z = z^2 + c does z tend towards infinity?
      float a = x;
      float b = y;
      int iter = 0;

      while (iter < maxIterations) {  // Here we determine whether or not we're out of bounds.
        float aa = a * a;
        float bb = b * b;
        float len = aa + bb;
        if (len > maxCalc) {  // |z| = sqrt(a^2+b^2) OR z^2 = a^2+b^2 to save on having to perform a square root.
          break;              // Bail
        }

        // This operation corresponds to z -> z^2+c where z=a+ib c=(x,y). Remember to use 'foil'.
        b = 2 * a * b + imAg;
        a = aa - bb + reAl;
        iter++;
      }  // while

      // We color each pixel based on how long it takes to get to infinity, or black if it never gets there.
      if (iter == maxIterations) {
        seg.set_pixel_color_xy(i, j, 0);
      } else {
        seg.set_pixel_color_xy(i, j,
                               seg.color_from_palette(iter * 255 / maxIterations, false, seg.palette_solid_wrap(), 0));
      }
      x += dx;
    }
    y += dy;
  }
  if (seg.check1)
    seg.blur(100, true);
}  // mode_2DJulia()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LISSAJOUS
//////////////////////////////
//     2D Lissajous         //
//////////////////////////////
void mode_2DLissajous(Segment &seg) {  // By: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  seg.fade_to_black_by(seg.intensity);
  uint_fast16_t phase = (seg.now * (1 + seg.custom3)) / 32;  // allow user to control rotation speed

  // for (int i=0; i < 4*(cols+rows); i ++) {
  for (int i = 0; i < 256; i++) {
    // float xlocn = float(sin8_t(now/4+i*(seg.speed>>5))) / 255.0f;
    // float ylocn = float(cos8_t(now/4+i*2)) / 255.0f;
    uint_fast8_t xlocn = sin8_t(phase / 2 + (i * seg.speed) / 32);
    uint_fast8_t ylocn = cos8_t(phase / 2 + i * 2);
    xlocn = (cols < 2) ? 1
                       : (wf_map(2 * xlocn, 0, 511, 0, 2 * (cols - 1)) + 1) /
                             2;  // softhack007: "(2* ..... +1) /2" for proper rounding
    ylocn = (rows < 2) ? 1
                       : (wf_map(2 * ylocn, 0, 511, 0, 2 * (rows - 1)) + 1) /
                             2;  // "rows > 1" is needed to avoid div/0 in map()
    seg.set_pixel_color_xy((uint8_t) xlocn, (uint8_t) ylocn,
                           seg.color_from_palette(seg.now / 100 + i, false, seg.palette_solid_wrap(), 0));
  }
  seg.blur(seg.custom1 >> (1 + seg.check1 * 3), seg.check1);
}  // mode_2DLissajous()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE2D
//////////////////////
//    2D Noise      //
//////////////////////
void mode_2Dnoise(Segment &seg) {  // By Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  const unsigned scale = seg.intensity + 2;

  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      uint8_t pixelHue8 = perlin8(x * scale, y * scale, seg.now / (16 - seg.speed / 16));
      seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), pixelHue8));
    }
  }
}  // mode_2Dnoise()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMA_BALL
//////////////////////////////
//     2D Plasma Ball       //
//////////////////////////////
void mode_2DPlasmaball(Segment &seg) {  // By: Stepko https://editor.soulmatelights.com/gallery/659-plasm-ball ,
                                        // Modified by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  seg.fade_to_black_by(seg.custom1 >> 2);
  uint_fast32_t t = (seg.now * 8) / (256 - seg.speed);  // optimized to avoid float
  for (int i = 0; i < cols; i++) {
    unsigned thisVal = perlin8(i * 30, t, t);
    unsigned thisMax = wf_map(thisVal, 0, 255, 0, cols - 1);
    for (int j = 0; j < rows; j++) {
      unsigned thisVal_ = perlin8(t, j * 30, t);
      unsigned thisMax_ = wf_map(thisVal_, 0, 255, 0, rows - 1);
      int x = (i + thisMax_ - cols / 2);
      int y = (j + thisMax - cols / 2);
      int cx = (i + thisMax_);
      int cy = (j + thisMax);

      seg.add_pixel_color_xy(i, j,
                             ((x - y > -2) && (x - y < 2)) || ((cols - 1 - x - y) > -2 && (cols - 1 - x - y < 2)) ||
                                     (cols - cx == 0) || (cols - 1 - cx == 0) ||
                                     ((rows - cy == 0) || (rows - 1 - cy == 0))
                                 ? ColorFromPalette(seg.palette_ref(), beat8(5, seg.now), thisVal, LINEARBLEND)
                                 : uint32_t(CRGB::Black));
    }
  }
  seg.blur(seg.custom2 >> 5);
}  // mode_2DPlasmaball()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POLAR_LIGHTS
////////////////////////////////
//  2D Polar Lights           //
////////////////////////////////

void mode_2DPolarLights(Segment &seg) {  // By: Kostyantyn Matviyevskyy
                                         // https://editor.soulmatelights.com/gallery/762-polar-lights , Modified by:
                                         // Andrew Tuline & @dedehai (palette support)
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
    seg.step = 0;
  }

  float adjustHeight = (float) wf_map(rows, 8, 32, 28, 12);  // maybe use mapf() ???
  unsigned adjScale = wf_map(cols, 8, 64, 310, 63);
  unsigned _scale = wf_map(seg.intensity, 0, 255, 30, adjScale);
  int _speed = wf_map(seg.speed, 0, 255, 128, 16);

  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      seg.step++;
      uint8_t palindex = qsub8(perlin8((seg.step % 2) + x * _scale, y * 16 + seg.step % 16, seg.step / _speed),
                               fabsf((float) rows / 2.0f - (float) y) * adjustHeight);
      uint8_t palbrightness = palindex;
      if (seg.check1)
        palindex = 255 - palindex;  // flip palette
      seg.set_pixel_color_xy(x, y, seg.color_from_palette(palindex, false, false, 255, palbrightness));
    }
  }
}  // mode_2DPolarLights()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PULSER
/////////////////////////
//     2D Pulser       //
/////////////////////////
void mode_2DPulser(Segment &seg) {  // By: ldirko   https://editor.soulmatelights.com/gallery/878-pulse-test ,
                                    // modifed by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  seg.fade_to_black_by(8 - (seg.intensity >> 5));
  uint32_t a = seg.now / (18 - seg.speed / 16);
  int x = (a / 14) % cols;
  int y = wf_map((sin8_t(a * 5) + sin8_t(a * 4) + sin8_t(a * 2)), 0, 765, rows - 1, 0);
  seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), wf_map(y, 0, rows - 1, 0, 255), 255, LINEARBLEND));

  seg.blur(seg.intensity >> 4);
}  // mode_2DPulser()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINDOTS
/////////////////////////
//     2D Sindots      //
/////////////////////////
void mode_2DSindots(Segment &seg) {  // By: ldirko   https://editor.soulmatelights.com/gallery/597-sin-dots ,
                                     // modified by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  seg.fade_to_black_by((seg.custom1 >> 3) + (seg.check1 * 24));

  uint8_t t1 = seg.now / (257 - seg.speed);  // 20;
  uint8_t t2 = sin8_t(t1) / 4 * 2;
  for (int i = 0; i < 13; i++) {
    int x = sin8_t(t1 + i * seg.intensity / 8) * (cols - 1) / 255;  // max index now 255x15/255=15!
    int y = sin8_t(t2 + i * seg.intensity / 8) * (rows - 1) / 255;  // max index now 255x15/255=15!
    seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), i * 255 / 13, 255, LINEARBLEND));
  }
  seg.blur(seg.custom2 >> (3 + seg.check1), seg.check1);
}  // mode_2DSindots()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SQUARED_SWIRL
//////////////////////////////
//     2D Squared Swirl     //
//////////////////////////////
// custom3 affects the blur amount.
void mode_2Dsquaredswirl(Segment &seg) {  // By: Mark Kriegsman.
                                          // https://gist.github.com/kriegsman/368b316c55221134b160
                                          // Modifed by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  const uint8_t kBorderWidth = 2;

  seg.fade_to_black_by(1 + seg.intensity / 5);
  seg.blur(seg.custom3 >> 1);

  // Use two out-of-sync sine waves
  int i = beatsin8_t(19, kBorderWidth, cols - kBorderWidth, seg.now);
  int j = beatsin8_t(22, kBorderWidth, cols - kBorderWidth, seg.now);
  int k = beatsin8_t(17, kBorderWidth, cols - kBorderWidth, seg.now);
  int m = beatsin8_t(18, kBorderWidth, rows - kBorderWidth, seg.now);
  int n = beatsin8_t(15, kBorderWidth, rows - kBorderWidth, seg.now);
  int p = beatsin8_t(20, kBorderWidth, rows - kBorderWidth, seg.now);

  seg.add_pixel_color_xy(i, m, ColorFromPalette(seg.palette_ref(), seg.now / 29, 255, LINEARBLEND));
  seg.add_pixel_color_xy(j, n, ColorFromPalette(seg.palette_ref(), seg.now / 41, 255, LINEARBLEND));
  seg.add_pixel_color_xy(k, p, ColorFromPalette(seg.palette_ref(), seg.now / 73, 255, LINEARBLEND));
}  // mode_2Dsquaredswirl()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SUN_RADIATION
//////////////////////////////
//     2D Sun Radiation     //
//////////////////////////////
void mode_2DSunradiation(Segment &seg) {  // By: ldirko
                                          // https://editor.soulmatelights.com/gallery/599-sun-radiation  , modified
                                          // by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (!seg.allocate_data(sizeof(uint8_t) * (cols + 2) * (rows + 2)))
    FX_FALLBACK_STATIC;  // allocation failed
  uint8_t *bump = reinterpret_cast<uint8_t *>(seg.data);

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  unsigned long t = seg.now / 4;
  unsigned index = 0;
  uint8_t someVal = seg.speed / 4;  // Was 25.
  for (int j = 0; j < (rows + 2); j++) {
    for (int i = 0; i < (cols + 2); i++) {
      uint8_t col = ((int16_t) perlin8(i * someVal, j * someVal, t) - 127) >> 2;  // about +/- 32
      bump[index++] = col;
    }
  }

  int yindex = cols + 3;
  int vly = -(rows / 2 + 1);
  for (int y = 0; y < rows; y++) {
    ++vly;
    int vlx = -(cols / 2 + 1);
    for (int x = 0; x < cols; x++) {
      ++vlx;
      int nx = bump[x + yindex + 1] - bump[x + yindex - 1];
      int ny = bump[x + yindex + (cols + 2)] - bump[x + yindex - (cols + 2)];
      unsigned difx = abs8(vlx * 7 - nx);
      unsigned dify = abs8(vly * 7 - ny);
      int temp = difx * difx + dify * dify;
      int col = 255 - temp / 8;  // 8 its a size of effect
      if (col < 0)
        col = 0;
      seg.set_pixel_color_xy(x, y, HeatColor(col / (3.0f - (float) (seg.intensity) / 128.f)));
    }
    yindex += (cols + 2);
  }
}  // mode_2DSunradiation()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TARTAN
/////////////////////////
//     2D Tartan       //
/////////////////////////
void mode_2Dtartan(Segment &seg) {  // By: Elliott Kember  https://editor.soulmatelights.com/gallery/3-tartan ,
                                    // Modified by: Andrew Tuline
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  if (seg.call == 0) {
    seg.fill(BLACK);
  }

  uint8_t hue, bri;
  size_t intensity;
  int offsetX = beatsin16_t(3, -360, 360, seg.now);
  int offsetY = beatsin16_t(2, -360, 360, seg.now);
  int sharpness = seg.custom3 / 8;  // 0-3

  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      hue = x * beatsin16_t(10, 1, 10, seg.now) + offsetY;
      intensity = bri = sin8_t(x * seg.speed / 2 + offsetX);
      for (int i = 0; i < sharpness; i++)
        intensity *= bri;
      intensity >>= 8 * sharpness;
      seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), hue, intensity, LINEARBLEND));
      hue = y * 3 + offsetX;
      intensity = bri = sin8_t(y * seg.intensity / 2 + offsetY);
      for (int i = 0; i < sharpness; i++)
        intensity *= bri;
      intensity >>= 8 * sharpness;
      seg.add_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), hue, intensity, LINEARBLEND));
    }
  }
}  // mode_2DTartan()
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPACESHIPS
/////////////////////////
//     2D spaceships   //
/////////////////////////
void mode_2Dspaceships(Segment &seg) {  //// Space ships by stepko (c)05.02.21
                                        //// [https://editor.soulmatelights.com/gallery/639-space-ships], adapted by
                                        //// Blaz Kristan (AKA blazoncek)
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  uint32_t tb = seg.now >> 12;  // every ~4s
  if (tb > seg.step) {
    int dir = ++seg.aux0;
    dir += (int) hw_random8(3) - 1;
    if (dir > 7)
      seg.aux0 = 0;
    else if (dir < 0)
      seg.aux0 = 7;
    else
      seg.aux0 = dir;
    seg.step = tb + hw_random8(4);
  }

  seg.fade_to_black_by(wf_map(seg.speed, 0, 255, 248, 16));
  seg.move(seg.aux0, 1);

  for (size_t i = 0; i < 8; i++) {
    int x = beatsin8_t(12 + i, 2, cols - 3, seg.now);
    int y = beatsin8_t(15 + i, 2, rows - 3, seg.now);
    uint32_t color = ColorFromPalette(seg.palette_ref(), beatsin8_t(12 + i, 0, 255, seg.now), 255);
    seg.add_pixel_color_xy(x, y, color);
    if (cols > 24 || rows > 24) {
      seg.add_pixel_color_xy(x + 1, y, color);
      seg.add_pixel_color_xy(x - 1, y, color);
      seg.add_pixel_color_xy(x, y + 1, color);
      seg.add_pixel_color_xy(x, y - 1, color);
    }
  }
  seg.blur(seg.intensity >> 3, seg.check1);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CRAZY_BEES
/////////////////////////
//     2D Crazy Bees   //
/////////////////////////
//// Crazy bees by stepko (c)12.02.21 [https://editor.soulmatelights.com/gallery/651-crazy-bees], adapted by Blaz
/// Kristan (AKA blazoncek), improved by @dedehai
constexpr int MAX_BEES = 5;
void mode_2Dcrazybees(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  Prng &prng = fx_prng();

  uint8_t n = std::min(MAX_BEES, (rows * cols) / 256 + 1);

  typedef struct Bee {
    uint8_t posX, posY, aimX, aimY, hue;
    int8_t deltaX, deltaY, signX, signY, error;
    void aimed(uint16_t w, uint16_t h) {
      // prng.setSeed(millis());
      Prng &prng = fx_prng();  // a local class cannot reach the enclosing scope's reference
      aimX = prng.random8(0, w);
      aimY = prng.random8(0, h);
      hue = prng.random8();
      deltaX = abs(aimX - posX);
      deltaY = abs(aimY - posY);
      signX = posX < aimX ? 1 : -1;
      signY = posY < aimY ? 1 : -1;
      error = deltaX - deltaY;
    };
  } bee_t;

  if (!seg.allocate_data(sizeof(bee_t) * MAX_BEES))
    FX_FALLBACK_STATIC;  // allocation failed
  bee_t *bee = reinterpret_cast<bee_t *>(seg.data);

  if (seg.call == 0) {
    prng.set_seed(seg.now);
    for (size_t i = 0; i < n; i++) {
      bee[i].posX = prng.random8(0, cols);
      bee[i].posY = prng.random8(0, rows);
      bee[i].aimed(cols, rows);
    }
  }

  if (seg.now > seg.step) {
    seg.step = seg.now + (FRAMETIME * 16 / ((seg.speed >> 4) + 1));
    seg.fade_to_black_by(32 + ((seg.check1 * seg.intensity) / 25));
    seg.blur(seg.intensity / (2 + seg.check1 * 9), seg.check1);
    for (size_t i = 0; i < n; i++) {
      uint32_t flowerCcolor = seg.color_from_palette(bee[i].hue, false, true, 255);
      seg.add_pixel_color_xy(bee[i].aimX + 1, bee[i].aimY, flowerCcolor);
      seg.add_pixel_color_xy(bee[i].aimX, bee[i].aimY + 1, flowerCcolor);
      seg.add_pixel_color_xy(bee[i].aimX - 1, bee[i].aimY, flowerCcolor);
      seg.add_pixel_color_xy(bee[i].aimX, bee[i].aimY - 1, flowerCcolor);
      if (bee[i].posX != bee[i].aimX || bee[i].posY != bee[i].aimY) {
        seg.set_pixel_color_xy(bee[i].posX, bee[i].posY, CRGB(CHSV(bee[i].hue, 60, 255)));
        int error2 = bee[i].error * 2;
        if (error2 > -bee[i].deltaY) {
          bee[i].error -= bee[i].deltaY;
          bee[i].posX += bee[i].signX;
        }
        if (error2 < bee[i].deltaX) {
          bee[i].error += bee[i].deltaX;
          bee[i].posY += bee[i].signY;
        }
      } else {
        bee[i].aimed(cols, rows);
      }
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GHOST_RIDER
/////////////////////////
//     2D Ghost Rider  //
/////////////////////////
//// Ghost Rider by stepko (c)2021 [https://editor.soulmatelights.com/gallery/716-ghost-rider], adapted by Blaz
/// Kristan (AKA blazoncek)
constexpr int LIGHTERS_AM = 64;  // max lighters (adequate for 32x32 matrix)
void mode_2Dghostrider(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  typedef struct Lighter {
    int16_t gPosX;
    int16_t gPosY;
    uint16_t gAngle;
    int8_t angleSpeed;
    uint16_t lightersPosX[LIGHTERS_AM];
    uint16_t lightersPosY[LIGHTERS_AM];
    uint16_t Angle[LIGHTERS_AM];
    uint16_t time[LIGHTERS_AM];
    bool reg[LIGHTERS_AM];
    int8_t Vspeed;
  } lighter_t;

  if (!seg.allocate_data(sizeof(lighter_t)))
    FX_FALLBACK_STATIC;  // allocation failed
  lighter_t *lighter = reinterpret_cast<lighter_t *>(seg.data);

  const size_t maxLighters = std::min(cols + rows, LIGHTERS_AM);

  if (seg.aux0 != cols || seg.aux1 != rows) {
    seg.aux0 = cols;
    seg.aux1 = rows;
    lighter->angleSpeed = hw_random8(0, 20) - 10;
    lighter->gAngle = hw_random16();
    lighter->Vspeed = 5;
    lighter->gPosX = (cols / 2) * 10;
    lighter->gPosY = (rows / 2) * 10;
    for (size_t i = 0; i < maxLighters; i++) {
      lighter->lightersPosX[i] = lighter->gPosX;
      lighter->lightersPosY[i] = lighter->gPosY + i;
      lighter->time[i] = i * 2;
      lighter->reg[i] = false;
    }
  }

  if (seg.now > seg.step) {
    seg.step = seg.now + 1024 / (cols + rows);

    seg.fade_to_black_by((seg.speed >> 2) + 64);

    CRGB color = CRGB::White;
    seg.wu_pixel(lighter->gPosX * 256 / 10, lighter->gPosY * 256 / 10, color);

    lighter->gPosX += lighter->Vspeed * sin_t(radians(lighter->gAngle));
    lighter->gPosY += lighter->Vspeed * cos_t(radians(lighter->gAngle));
    lighter->gAngle += lighter->angleSpeed;
    if (lighter->gPosX < 0)
      lighter->gPosX = (cols - 1) * 10;
    if (lighter->gPosX > (cols - 1) * 10)
      lighter->gPosX = 0;
    if (lighter->gPosY < 0)
      lighter->gPosY = (rows - 1) * 10;
    if (lighter->gPosY > (rows - 1) * 10)
      lighter->gPosY = 0;
    for (size_t i = 0; i < maxLighters; i++) {
      lighter->time[i] += hw_random8(5, 20);
      if (lighter->time[i] >= 255 || (lighter->lightersPosX[i] <= 0) ||
          (lighter->lightersPosX[i] >= (cols - 1) * 10) || (lighter->lightersPosY[i] <= 0) ||
          (lighter->lightersPosY[i] >= (rows - 1) * 10)) {
        lighter->reg[i] = true;
      }
      if (lighter->reg[i]) {
        lighter->lightersPosY[i] = lighter->gPosY;
        lighter->lightersPosX[i] = lighter->gPosX;
        lighter->Angle[i] = lighter->gAngle + ((int) hw_random8(20) - 10);
        lighter->time[i] = 0;
        lighter->reg[i] = false;
      } else {
        lighter->lightersPosX[i] += -7 * sin_t(radians(lighter->Angle[i]));
        lighter->lightersPosY[i] += -7 * cos_t(radians(lighter->Angle[i]));
      }
      seg.wu_pixel(lighter->lightersPosX[i] * 256 / 10, lighter->lightersPosY[i] * 256 / 10,
                   ColorFromPalette(seg.palette_ref(), (256 - lighter->time[i])));
    }
    seg.blur(seg.intensity >> 3);
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLOBS
////////////////////////////
//     2D Floating Blobs  //
////////////////////////////
//// Floating Blobs by stepko (c)2021 [https://editor.soulmatelights.com/gallery/573-blobs], adapted by Blaz Kristan
/// (AKA blazoncek)
constexpr int MAX_BLOBS = 8;
void mode_2Dfloatingblobs(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  typedef struct Blob {
    float x[MAX_BLOBS], y[MAX_BLOBS];
    float sX[MAX_BLOBS], sY[MAX_BLOBS];  // speed
    float r[MAX_BLOBS];
    bool grow[MAX_BLOBS];
    uint8_t color[MAX_BLOBS];
  } blob_t;

  size_t Amount = (seg.intensity >> 5) + 1;  // NOTE: be sure to update MAX_BLOBS if you change this

  if (!seg.allocate_data(sizeof(blob_t)))
    FX_FALLBACK_STATIC;  // allocation failed
  blob_t *blob = reinterpret_cast<blob_t *>(seg.data);

  if (seg.aux0 != cols || seg.aux1 != rows) {
    seg.aux0 = cols;  // re-initialise if virtual size changes
    seg.aux1 = rows;
    // seg.fill(BLACK);
    for (size_t i = 0; i < MAX_BLOBS; i++) {
      blob->r[i] = hw_random8(1, cols > 8 ? (cols / 4) : 2);
      blob->sX[i] = (float) hw_random8(3, cols) / (float) (256 - seg.speed);  // speed x
      blob->sY[i] = (float) hw_random8(3, rows) / (float) (256 - seg.speed);  // speed y
      blob->x[i] = hw_random8(0, cols - 1);
      blob->y[i] = hw_random8(0, rows - 1);
      blob->color[i] = hw_random8();
      blob->grow[i] = (blob->r[i] < 1.f);
      if (blob->sX[i] == 0)
        blob->sX[i] = 1;
      if (blob->sY[i] == 0)
        blob->sY[i] = 1;
    }
  }

  seg.fade_to_black_by((seg.custom2 >> 3) + 1);

  // Bounce balls around
  for (size_t i = 0; i < Amount; i++) {
    if (seg.step < seg.now)
      blob->color[i] += 4;  // slowly change color
    // change radius if needed
    if (blob->grow[i]) {
      // enlarge radius until it is >= 4
      blob->r[i] += (fabsf(blob->sX[i]) > fabsf(blob->sY[i]) ? fabsf(blob->sX[i]) : fabsf(blob->sY[i])) * 0.05f;
      if (blob->r[i] >= std::min(cols / 4.f, 2.f)) {
        blob->grow[i] = false;
      }
    } else {
      // reduce radius until it is < 1
      blob->r[i] -= (fabsf(blob->sX[i]) > fabsf(blob->sY[i]) ? fabsf(blob->sX[i]) : fabsf(blob->sY[i])) * 0.05f;
      if (blob->r[i] < 1.f) {
        blob->grow[i] = true;
      }
    }
    uint32_t c = seg.color_from_palette(blob->color[i], false, false, 0);
    if (blob->r[i] > 1.f)
      seg.fill_circle(roundf(blob->x[i]), roundf(blob->y[i]), roundf(blob->r[i]), c);
    else
      seg.set_pixel_color_xy((int) roundf(blob->x[i]), (int) roundf(blob->y[i]), c);
    // move x
    if (blob->x[i] + blob->r[i] >= cols - 1)
      blob->x[i] += (blob->sX[i] * ((cols - 1 - blob->x[i]) / blob->r[i] + 0.005f));
    else if (blob->x[i] - blob->r[i] <= 0)
      blob->x[i] += (blob->sX[i] * (blob->x[i] / blob->r[i] + 0.005f));
    else
      blob->x[i] += blob->sX[i];
    // move y
    if (blob->y[i] + blob->r[i] >= rows - 1)
      blob->y[i] += (blob->sY[i] * ((rows - 1 - blob->y[i]) / blob->r[i] + 0.005f));
    else if (blob->y[i] - blob->r[i] <= 0)
      blob->y[i] += (blob->sY[i] * (blob->y[i] / blob->r[i] + 0.005f));
    else
      blob->y[i] += blob->sY[i];
    // bounce x
    if (blob->x[i] < 0.01f) {
      blob->sX[i] = (float) hw_random8(3, cols) / (256 - seg.speed);
      blob->x[i] = 0.01f;
    } else if (blob->x[i] > (float) cols - 1.01f) {
      blob->sX[i] = (float) hw_random8(3, cols) / (256 - seg.speed);
      blob->sX[i] = -blob->sX[i];
      blob->x[i] = (float) cols - 1.01f;
    }
    // bounce y
    if (blob->y[i] < 0.01f) {
      blob->sY[i] = (float) hw_random8(3, rows) / (256 - seg.speed);
      blob->y[i] = 0.01f;
    } else if (blob->y[i] > (float) rows - 1.01f) {
      blob->sY[i] = (float) hw_random8(3, rows) / (256 - seg.speed);
      blob->sY[i] = -blob->sY[i];
      blob->y[i] = (float) rows - 1.01f;
    }
  }
  seg.blur(seg.custom1 >> 2);

  if (seg.step < seg.now)
    seg.step = seg.now + 2000;  // change colors every 2 seconds
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIFT_ROSE
////////////////////////////
//     2D Drift Rose      //
////////////////////////////
//// Drift Rose by stepko (c)2021 [https://editor.soulmatelights.com/gallery/1369-drift-rose-pattern], adapted by
/// Blaz Kristan (AKA blazoncek) improved by @dedehai
void mode_2Ddriftrose(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  const float CX = (cols - cols % 2) / 2.f - .5f;
  const float CY = (rows - rows % 2) / 2.f - .5f;
  const float L = std::min(cols, rows) / 2.f;

  seg.fade_to_black_by(32 + (seg.speed >> 3));
  for (size_t i = 1; i < 37; i++) {
    float angle = radians(i * 10);
    uint32_t x = (CX + (sin_t(angle) * (beatsin8_t(i, 0, L * 2, seg.now) - L))) * 255.f;
    uint32_t y = (CY + (cos_t(angle) * (beatsin8_t(i, 0, L * 2, seg.now) - L))) * 255.f;
    if (seg.palette == 0)
      seg.wu_pixel(x, y, CHSV(i * 10, 255, 255));
    else
      seg.wu_pixel(x, y, ColorFromPalette(seg.palette_ref(), i * 10));
  }
  seg.blur(seg.intensity >> 4, seg.check1);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROTOZOOMER
/////////////////////////////
//  2D PLASMA ROTOZOOMER   //
/////////////////////////////
// Plasma Rotozoomer by ldirko (c)2020 [https://editor.soulmatelights.com/gallery/457-plasma-rotozoomer], adapted for
// WLED by Blaz Kristan (AKA blazoncek)
void mode_2Dplasmarotozoom(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  unsigned dataSize = seg.length() + sizeof(float);
  if (!seg.allocate_data(dataSize))
    FX_FALLBACK_STATIC;  // allocation failed
  float *a = reinterpret_cast<float *>(seg.data);
  uint8_t *plasma = reinterpret_cast<uint8_t *>(seg.data + sizeof(float));

  unsigned ms = seg.now / 15;

  // plasma
  for (int j = 0; j < rows; j++) {
    int index = j * cols;
    for (int i = 0; i < cols; i++) {
      if (seg.check1)
        plasma[index + i] = (i * 4 ^ j * 4) + ms / 6;
      else
        plasma[index + i] = perlin8(i * 40, j * 40, ms);
    }
  }

  // rotozoom
  float f = (sin_t(*a / 2) + ((128 - seg.intensity) / 128.0f) + 1.1f) / 1.5f;  // scale factor
  float kosinus = cos_t(*a) * f;
  float sinus = sin_t(*a) * f;
  for (int i = 0; i < cols; i++) {
    float u1 = i * kosinus;
    float v1 = i * sinus;
    for (int j = 0; j < rows; j++) {
      uint8_t u = abs8(u1 - j * sinus) % cols;
      uint8_t v = abs8(v1 + j * kosinus) % rows;
      seg.set_pixel_color_xy(i, j, seg.color_from_palette(plasma[v * cols + u], false, seg.palette_solid_wrap(), 255));
    }
  }
  *a -= 0.03f + float(seg.speed - 128) * 0.0002f;  // rotation speed
  if (*a < -6283.18530718f)
    *a += 6283.18530718f;  // 1000*2*PI, protect sin/cos from very large input float values (will give wrong results)
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISTORTION_WAVES
// Distortion waves - ldirko
// https://editor.soulmatelights.com/gallery/1089-distorsion-waves
// adapted for WLED by @blazoncek, improvements by @dedehai
void mode_2Ddistortionwaves(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  uint8_t speed = seg.speed / 32;
  uint8_t scale = seg.intensity / 32;
  if (seg.check2)
    scale += 192 / (cols + rows);  // zoom out some more. note: not changing scale slider for backwards compatibility

  unsigned a = seg.now / 32;
  unsigned a2 = a / 2;
  unsigned a3 = a / 3;
  unsigned colsScaled = cols * scale;
  unsigned rowsScaled = rows * scale;

  unsigned cx = beatsin16_t(10 - speed, 0, colsScaled, seg.now);
  unsigned cy = beatsin16_t(12 - speed, 0, rowsScaled, seg.now);
  unsigned cx1 = beatsin16_t(13 - speed, 0, colsScaled, seg.now);
  unsigned cy1 = beatsin16_t(15 - speed, 0, rowsScaled, seg.now);
  unsigned cx2 = beatsin16_t(17 - speed, 0, colsScaled, seg.now);
  unsigned cy2 = beatsin16_t(14 - speed, 0, rowsScaled, seg.now);

  uint8_t rdistort, gdistort, bdistort;

  unsigned xoffs = 0;
  for (int x = 0; x < cols; x++) {
    xoffs += scale;
    unsigned yoffs = 0;

    for (int y = 0; y < rows; y++) {
      yoffs += scale;

      if (seg.check3) {
        // alternate mode from original code
        rdistort = cos8_t(((x + y) * 8 + a2) & 255) >> 1;
        gdistort = cos8_t(((x + y) * 8 + a3 + 32) & 255) >> 1;
        bdistort = cos8_t(((x + y) * 8 + a + 64) & 255) >> 1;
      } else {
        rdistort = cos8_t((cos8_t(((x << 3) + a) & 255) + cos8_t(((y << 3) - a2) & 255) + a3) & 255) >> 1;
        gdistort = cos8_t((cos8_t(((x << 3) - a2) & 255) + cos8_t(((y << 3) + a3) & 255) + a + 32) & 255) >> 1;
        bdistort = cos8_t((cos8_t(((x << 3) + a3) & 255) + cos8_t(((y << 3) - a) & 255) + a2 + 64) & 255) >> 1;
      }

      uint8_t valueR = rdistort + ((a - (((xoffs - cx) * (xoffs - cx) + (yoffs - cy) * (yoffs - cy)) >> 7)) << 1);
      uint8_t valueG = gdistort + ((a2 - (((xoffs - cx1) * (xoffs - cx1) + (yoffs - cy1) * (yoffs - cy1)) >> 7)) << 1);
      uint8_t valueB = bdistort + ((a3 - (((xoffs - cx2) * (xoffs - cx2) + (yoffs - cy2) * (yoffs - cy2)) >> 7)) << 1);

      valueR = cos8_t(valueR);
      valueG = cos8_t(valueG);
      valueB = cos8_t(valueB);

      if (seg.palette == 0) {
        // use RGB values (original color mode)
        seg.set_pixel_color_xy(x, y, RGBW32(valueR, valueG, valueB, 0));
      } else {
        // use palette
        uint8_t brightness = (valueR + valueG + valueB) / 3;
        if (seg.check1) {  // map brightness to palette index
          seg.set_pixel_color_xy(x, y,
                                 ColorFromPalette(seg.palette_ref(), brightness, 255, LINEARBLEND_NOWRAP));
        } else {
          // color mapping: calculate hue from pixel color, map it to palette index
          CHSV hsvclr = rgb2hsv(CRGB(valueR >> 2, valueG >> 2,
                                     valueB >> 2));  // scale colors down to not saturate for better hue extraction
          seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), hsvclr.h, brightness));
        }
      }
    }
  }

  // palette mode and not filling: smear-blur to cover up palette wrapping artefacts
  if (!seg.check1 && seg.palette)
    seg.blur(200, true);
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOAP
// Soap
//@Stepko
// Idea from https://www.youtube.com/watch?v=DiHBgITrZck&ab_channel=StefanPetrick
//  adapted for WLED by @blazoncek, optimization by @dedehai
//
// Upstream keeps the one row/column scratch buffer as a stack VLA sized from the
// matrix; VLAs are not allowed here, so mode_2Dsoap allocates it alongside its
// other data and passes it in.
void soapPixels(Segment &seg, bool isRow, uint8_t *noise3d, CRGB *pixels, CRGB *ledsbuff) {
  const int cols = seg.width();
  const int rows = seg.height();
  const auto XY = [&](int x, int y) { return x + y * cols; };
  const auto abs = [](int x) { return x < 0 ? -x : x; };
  const int tRC = isRow ? rows : cols;  // transpose if isRow
  const int tCR = isRow ? cols : rows;  // transpose if isRow
  const int amplitude = std::max(1, (tCR - 8) >> 3) * (1 + (seg.custom1 >> 5));
  const int shift = 0;  //(128 - seg.custom2)*2;

  for (int i = 0; i < tRC; i++) {
    int amount = ((int) noise3d[isRow ? i * cols : i] - 128) * amplitude +
                 shift;  // use first row/column: XY(0,i)/XY(i,0)
    int delta = abs(amount) >> 8;
    int fraction = abs(amount) & 255;
    for (int j = 0; j < tCR; j++) {
      int zD, zF;
      if (amount < 0) {
        zD = j - delta;
        zF = zD - 1;
      } else {
        zD = j + delta;
        zF = zD + 1;
      }
      int yA = abs(zD) % tCR;
      int yB = abs(zF) % tCR;
      int xA = i;
      int xB = i;
      if (isRow) {
        std::swap(xA, yA);
        std::swap(xB, yB);
      }
      const int indxA = XY(xA, yA);
      const int indxB = XY(xB, yB);
      CRGB PixelA;
      CRGB PixelB;
      if ((zD >= 0) && (zD < tCR))
        PixelA = pixels[indxA];
      else
        PixelA = ColorFromPalette(seg.palette_ref(), ~noise3d[indxA] * 3);
      if ((zF >= 0) && (zF < tCR))
        PixelB = pixels[indxB];
      else
        PixelB = ColorFromPalette(seg.palette_ref(), ~noise3d[indxB] * 3);
      ledsbuff[j] = (PixelA.nscale8(ease8_in_out_cubic(255 - fraction))) + (PixelB.nscale8(ease8_in_out_cubic(fraction)));
    }
    for (int j = 0; j < tCR; j++) {
      CRGB c = ledsbuff[j];
      if (isRow)
        std::swap(j, i);
      seg.set_pixel_color_xy(i, j, pixels[XY(i, j)] = c);
      if (isRow)
        std::swap(j, i);
    }
  }
}

void mode_2Dsoap(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  const auto XY = [&](int x, int y) { return x + y * cols; };

  const size_t segSize = seg.width() * seg.height();  // prevent reallocation if mirrored or grouped
  const size_t dataSize = segSize * (sizeof(uint8_t) + sizeof(CRGB));  // pixels and noise
  // The trailing allocation is the row/column scratch buffer that upstream puts on
  // the stack as a VLA.
  const size_t buffSize = static_cast<size_t>(std::max(cols, rows)) * sizeof(CRGB);
  if (!seg.allocate_data(dataSize + sizeof(uint32_t) * 3 + buffSize))
    FX_FALLBACK_STATIC;  // allocation failed

  uint8_t *noise3d = reinterpret_cast<uint8_t *>(seg.data);
  CRGB *pixels = reinterpret_cast<CRGB *>(seg.data + segSize * sizeof(uint8_t));
  uint32_t *noisecoord = reinterpret_cast<uint32_t *>(seg.data + dataSize);  // x, y, z coordinates
  CRGB *ledsbuff = reinterpret_cast<CRGB *>(seg.data + dataSize + sizeof(uint32_t) * 3);
  const uint32_t scale32_x = 160000U / cols;
  const uint32_t scale32_y = 160000U / rows;
  const uint32_t mov = std::min(cols, rows) * (seg.speed + 2) / 2;
  const uint8_t smoothness = std::min<int>(250, seg.intensity);  // limit as >250 produces very little changes

  if (seg.call == 0)
    for (int i = 0; i < 3; i++)
      noisecoord[i] = hw_random();  // init
  else
    for (int i = 0; i < 3; i++)
      noisecoord[i] += mov;

  for (int i = 0; i < cols; i++) {
    int32_t ioffset = scale32_x * (i - cols / 2);
    for (int j = 0; j < rows; j++) {
      int32_t joffset = scale32_y * (j - rows / 2);
      uint8_t data = perlin16(noisecoord[0] + ioffset, noisecoord[1] + joffset, noisecoord[2]) >> 8;
      noise3d[XY(i, j)] = scale8(noise3d[XY(i, j)], smoothness) + scale8(data, 255 - smoothness);
    }
  }
  // init also if dimensions changed
  if (seg.call == 0 || seg.aux0 != cols || seg.aux1 != rows) {
    seg.aux0 = cols;
    seg.aux1 = rows;
    for (int i = 0; i < cols; i++) {
      for (int j = 0; j < rows; j++) {
        seg.set_pixel_color_xy(i, j, ColorFromPalette(seg.palette_ref(), ~noise3d[XY(i, j)] * 3));
      }
    }
  }

  soapPixels(seg, true, noise3d, pixels, ledsbuff);   // rows
  soapPixels(seg, false, noise3d, pixels, ledsbuff);  // cols
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_OCTOPUS
// Idea from https://www.youtube.com/watch?v=HsA-6KIbgto&ab_channel=GreatScott%21
// Octopus (https://editor.soulmatelights.com/gallery/671-octopus)
// Stepko and Sutaburosu
//  adapted for WLED by @blazoncek
void mode_2Doctopus(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();
  const auto XY = [&](int x, int y) { return (x % cols) + (y % rows) * cols; };
  const uint8_t mapp = 180 / std::max(cols, rows);

  typedef struct {
    uint8_t angle;
    uint8_t radius;
  } map_t;

  const size_t dataSize = seg.width() * seg.height() * sizeof(map_t);  // prevent reallocation if mirrored or grouped
  if (!seg.allocate_data(dataSize + 2))
    FX_FALLBACK_STATIC;  // allocation failed

  map_t *rMap = reinterpret_cast<map_t *>(seg.data);
  uint8_t *offsX = reinterpret_cast<uint8_t *>(seg.data + dataSize);
  uint8_t *offsY = reinterpret_cast<uint8_t *>(seg.data + dataSize + 1);

  // re-init if seg dimensions or offset changed
  if (seg.call == 0 || seg.aux0 != cols || seg.aux1 != rows || seg.custom1 != *offsX || seg.custom2 != *offsY) {
    seg.step = 0;  // t
    seg.aux0 = cols;
    seg.aux1 = rows;
    *offsX = seg.custom1;
    *offsY = seg.custom2;
    const int C_X = (cols / 2) + ((seg.custom1 - 128) * cols) / 255;
    const int C_Y = (rows / 2) + ((seg.custom2 - 128) * rows) / 255;
    for (int x = 0; x < cols; x++) {
      for (int y = 0; y < rows; y++) {
        int dx = (x - C_X);
        int dy = (y - C_Y);
        rMap[XY(x, y)].angle = int(40.7436f * atan2_t(dy, dx));   // avoid 128*atan2()/PI
        rMap[XY(x, y)].radius = sqrtf(dx * dx + dy * dy) * mapp;  // thanks Sutaburosu
      }
    }
  }

  seg.step += seg.speed / 32 + 1;  // 1-4 range
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      uint8_t angle = rMap[XY(x, y)].angle;
      uint8_t radius = rMap[XY(x, y)].radius;
      // CRGB c = CHSV(seg.step / 2 - radius, 255, sin8_t(sin8_t((angle * 4 - radius) / 4 + seg.step) + radius -
      // seg.step * 2 + angle * (seg.custom3/3+1)));
      unsigned intensity =
          sin8_t(sin8_t((angle * 4 - radius) / 4 + seg.step / 2) + radius - seg.step + angle * (seg.custom3 / 4 + 1));
      // intensity = map((intensity*intensity) & 0xFFFF, 0, 65535, 0, 255); // add a bit of non-linearity for cleaner
      // display -> no longer needed with proper gamma correction
      seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), seg.step / 2 - radius, intensity));
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVING_CELL
// Waving Cell
//@Stepko (https://editor.soulmatelights.com/gallery/1704-wavingcells)
//  adapted for WLED by @blazoncek, improvements by @dedehai
void mode_2Dwavingcell(Segment &seg) {
  if (!seg.is_2d())
    FX_FALLBACK_STATIC;  // not a 2D set-up

  const int cols = seg.width();
  const int rows = seg.height();

  uint32_t t = (seg.now * (seg.speed + 1)) >> 3;
  uint32_t aX = seg.custom1 / 16 + 9;
  uint32_t aY = seg.custom2 / 16 + 1;
  uint32_t aZ = seg.custom3 + 1;
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      uint32_t wave = sin8_t((x * aX) + sin8_t((((y << 8) + t) * aY) >> 8)) +
                      cos8_t(y * aZ);  // bit shifts to increase temporal resolution
      uint8_t colorIndex = wave + (t >> (8 - (seg.check2 * 3)));
      seg.set_pixel_color_xy(x, y, ColorFromPalette(seg.palette_ref(), colorIndex));
    }
  }
  seg.blur(seg.intensity);
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLACK_HOLE
    {"Black Hole@Fade rate,Outer Y freq.,Outer X freq.,Inner X freq.,Inner Y freq.,Solid,,Blur;!;!;2;pal=11",
     mode_2DBlackHole},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_COLORED_BURSTS
    {"Colored Bursts@Speed,# of lines,,,Blur,Gradient,Smear,Dots;;!;2;c3=16", mode_2DColoredBursts},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DNA
    {"DNA@Scroll speed,Blur,,,,Smear;;!;2;ix=0", mode_2Ddna},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DNA_SPIRAL
    {"DNA Spiral@Scroll speed,Y frequency,Blur,,,Smear;;!;2;c1=0", mode_2DDNASpiral},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIFT
    {"Drift@Rotation speed,Blur,,,,Twin,Smear;;!;2;ix=0", mode_2DDrift},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FIRENOISE
    {"Firenoise@X scale,Y scale,,,,Palette;;!;2;pal=66", mode_2Dfirenoise},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_FRIZZLES
    {"Frizzles@X frequency,Y frequency,Blur,,,Smear;;!;2", mode_2DFrizzles},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GAME_OF_LIFE
    {"Game Of Life@!,,Blur,,,,,Mutation;!,!;!;2;pal=11,sx=128", mode_2Dgameoflife},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_HIPHOTIC
    {"Hiphotic@X scale,Y scale,,,Speed;!;!;2", mode_2DHiphotic},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_JULIA
    {"Julia@,Max iterations per pixel,X center,Y center,Area size, Blur;!;!;2;ix=24,c1=128,c2=128,c3=16",
     mode_2DJulia},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_LISSAJOUS
    {"Lissajous@X frequency,Fade rate,Blur,,Speed,Smear;!;!;2;c1=0", mode_2DLissajous},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_NOISE2D
    {"Noise2D@!,Scale;;!;2", mode_2Dnoise},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PLASMA_BALL
    {"Plasma Ball@Speed,,Fade,Blur;;!;2", mode_2DPlasmaball},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_POLAR_LIGHTS
    {"Polar Lights@!,Scale,,,,Flip Palette;;!;2;pal=71", mode_2DPolarLights},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PULSER
    {"Pulser@!,Blur;;!;2", mode_2DPulser},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SINDOTS
    {"Sindots@!,Dot distance,Fade rate,Blur,,Smear;;!;2;", mode_2DSindots},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SQUARED_SWIRL
    {"Squared Swirl@,Fade,,,Blur;;!;2", mode_2Dsquaredswirl},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SUN_RADIATION
    {"Sun Radiation@Variance,Brightness;;;2", mode_2DSunradiation},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_TARTAN
    {"Tartan@X scale,Y scale,,,Sharpness;;!;2", mode_2Dtartan},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SPACESHIPS
    {"Spaceships@!,Blur,,,,Smear;;!;2", mode_2Dspaceships},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_CRAZY_BEES
    {"Crazy Bees@!,Blur,,,,Smear;;!;2;pal=11,ix=0", mode_2Dcrazybees},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_GHOST_RIDER
    {"Ghost Rider@Fade rate,Blur;;!;2", mode_2Dghostrider},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_BLOBS
    {"Blobs@!,# blobs,Blur,Trail;!;!;2;c1=8", mode_2Dfloatingblobs},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DRIFT_ROSE
    {"Drift Rose@Fade,Blur,,,,Smear;;!;2;pal=11", mode_2Ddriftrose},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_ROTOZOOMER
    {"Rotozoomer@!,Scale,,,,Alt;;!;2;pal=54", mode_2Dplasmarotozoom},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_DISTORTION_WAVES
    {"Distortion Waves@!,Scale,,,,Fill,Zoom,Alt;;!;2;pal=0", mode_2Ddistortionwaves},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_SOAP
    {"Soap@!,Smoothness,Density;;!;2;pal=11", mode_2Dsoap},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_OCTOPUS
    {"Octopus@!,,Offset X,Offset Y,Legs,fasttan;;!;2;", mode_2Doctopus},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_WAVING_CELL
    {"Waving Cell@!,Blur,Amplitude 1,Amplitude 2,Amplitude 3,,Flow;;!;2;ix=0", mode_2Dwavingcell},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_2D_B;
const EffectGroup EFFECT_GROUP_2D_B{"2d_b", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_2D_B
