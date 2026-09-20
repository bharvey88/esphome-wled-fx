// Behaviour tests for the engine that the contact sheet run cannot express.
//
// The main simulator answers "does every effect draw something without walking
// off the end of the canvas". These are the questions that need a specific
// effect at a specific size over a specific number of frames, and that a still
// image would not have caught: which way Scrolling Text moves, whether a
// control the user moved leaks into the next effect, and whether custom3 is
// really the five bit value the effects treat it as.
//
// Exit status is the number of failures.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../components/wled_fx/wf_engine.h"
#include "../../components/wled_fx/wf_math.h"
#include "../../components/wled_fx/wf_particle.h"
#include "../../components/wled_fx/wf_registry.h"

using namespace esphome::wled_fx;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
  if (!ok)
    failures++;
}

struct Frame {
  int width{0};
  int height{0};
  std::vector<uint8_t> lit;  // one byte per pixel, non-zero when the pixel is on
};

Frame capture(const Canvas &canvas) {
  Frame f;
  f.width = canvas.width();
  f.height = canvas.height();
  f.lit.resize(static_cast<size_t>(f.width) * f.height);
  for (size_t i = 0; i < f.lit.size(); i++)
    f.lit[i] = (canvas.get(i) & 0x00FFFFFFu) != 0 ? 1 : 0;
  return f;
}

struct Bounds {
  int min_x{0}, max_x{0}, min_y{0}, max_y{0};
  size_t count{0};
  bool empty() const { return this->count == 0; }
  int width() const { return this->max_x - this->min_x + 1; }
  int height() const { return this->max_y - this->min_y + 1; }
};

Bounds bounds_of(const Frame &f) {
  Bounds b;
  b.min_x = f.width;
  b.min_y = f.height;
  b.max_x = -1;
  b.max_y = -1;
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width; x++) {
      if (!f.lit[static_cast<size_t>(y) * f.width + x])
        continue;
      b.count++;
      if (x < b.min_x)
        b.min_x = x;
      if (x > b.max_x)
        b.max_x = x;
      if (y < b.min_y)
        b.min_y = y;
      if (y > b.max_y)
        b.max_y = y;
    }
  }
  return b;
}

/* The (dx, dy) that lines frame `a` up with frame `b` best, searched over a
 * small window. This is what says "the picture moved sideways" rather than
 * "some pixels changed", which is the whole question for a scrolling effect. */
void best_shift(const Frame &a, const Frame &b, int range, int &out_dx, int &out_dy) {
  long best_score = -1;
  int best_distance = 0;
  out_dx = 0;
  out_dy = 0;
  for (int dy = -range; dy <= range; dy++) {
    for (int dx = -range; dx <= range; dx++) {
      long score = 0;
      for (int y = 0; y < a.height; y++) {
        const int sy = y + dy;
        if (sy < 0 || sy >= a.height)
          continue;
        for (int x = 0; x < a.width; x++) {
          const int sx = x + dx;
          if (sx < 0 || sx >= a.width)
            continue;
          if (a.lit[static_cast<size_t>(y) * a.width + x] && b.lit[static_cast<size_t>(sy) * a.width + sx])
            score++;
        }
      }
      // Ties go to the smaller shift, so a still picture reads as (0, 0)
      // rather than as whichever corner of the search window came first.
      const int distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
      if (score > best_score || (score == best_score && distance < best_distance)) {
        best_score = score;
        best_distance = distance;
        out_dx = dx;
        out_dy = dy;
      }
    }
  }
}

// Renders `frames` frames at WLED's nominal 23 ms and hands back the last one.
Frame run(Engine &engine, int frames, uint32_t &now) {
  Frame last;
  for (int i = 0; i < frames; i++) {
    engine.render(now);
    now += 23;
    last = capture(engine.canvas());
  }
  return last;
}

// --- Scrolling Text ---------------------------------------------------------

void test_scrolling_text() {
  printf("Scrolling Text\n");

  const EffectInfo *info = EffectRegistry::find("Scrolling Text");
  if (info == nullptr) {
    check(false, "Scrolling Text is registered");
    return;
  }

  /* WLED packs the rotation into custom3, which is a five bit control: the
   * effect maps 0..31 onto -2..+2 and 16 is the metadata default, so the text
   * has to come out upright with no control touched. A custom3 declared or
   * published on a 0..255 range would land in the rotated part of that map. */
  const EffectDefaults defaults = effect_defaults(*info);
  check(defaults.custom3 == 16, "custom3 defaults to 16");
  check(wf_map(defaults.custom3, 0, 31, -2, 2) == 0, "the default custom3 is no rotation");

  /* Long enough not to fit across 64 columns, which is the branch that
   * scrolls. WLED positions text that does fit in the centre and leaves it
   * there, so the short case is tested separately below rather than treated
   * as a bug. */
  {
    Engine engine;
    check(engine.init(64, 64), "64x64 canvas");
    check(engine.set_effect("Scrolling Text"), "selected by name");
    engine.set_text("WLED FX SCROLLING TEXT");

    uint32_t now = 1000;
    run(engine, 40, now);  // let the first scroll step land
    const Frame a = capture(engine.canvas());
    run(engine, 40, now);
    const Frame b = capture(engine.canvas());

    const Bounds ba = bounds_of(a);
    check(!ba.empty(), "something is drawn");
    // A 6x8 font upright is eight rows tall. Rotated ninety degrees each glyph
    // would be six rows tall and eight columns wide, so the row extent is what
    // tells the two apart without depending on which glyphs are on screen.
    check(ba.height() <= 8, "the glyphs are upright, not rotated on their side");

    int dx = 0, dy = 0;
    best_shift(a, b, 12, dx, dy);
    printf("      shift over 40 frames: dx=%d dy=%d\n", dx, dy);
    check(dx < 0, "the text moves right to left");
    check(dy == 0, "the text does not drift up or down");
  }

  // Text that fits is centred and still, which is what WLED does. The hardware
  // review read this as a fault, so it is written down here as the expectation.
  {
    Engine engine;
    engine.init(64, 64);
    engine.set_effect("Scrolling Text");
    engine.set_text("WLED FX");

    uint32_t now = 1000;
    run(engine, 40, now);
    const Frame a = capture(engine.canvas());
    run(engine, 80, now);
    const Frame b = capture(engine.canvas());

    int dx = 0, dy = 0;
    best_shift(a, b, 12, dx, dy);
    check(dx == 0 && dy == 0, "text that fits the panel is held still");
    const Bounds ba = bounds_of(a);
    const int left_margin = ba.min_x;
    const int right_margin = 63 - ba.max_x;
    check(left_margin > 0 && right_margin > 0 && (left_margin - right_margin) <= 1 &&
              (right_margin - left_margin) <= 1,
          "text that fits the panel is centred");
  }

  /* Intensity is the one control that changes which of those two branches
   * runs, which is why a value left over from the previous effect was enough
   * to make the text travel down the panel instead of across it. */
  {
    Engine engine;
    engine.init(64, 64);
    engine.set_effect("Scrolling Text");
    engine.set_text("WLED FX");
    engine.set_intensity(255);

    uint32_t now = 1000;
    run(engine, 20, now);
    const Frame a = capture(engine.canvas());
    // Ten frames, because the vertical sweep crosses the whole panel in about
    // three seconds and a longer gap would run off the bottom.
    run(engine, 10, now);
    const Frame b = capture(engine.canvas());
    int dx = 0, dy = 0;
    best_shift(a, b, 20, dx, dy);
    printf("      with intensity pinned at 255: dx=%d dy=%d\n", dx, dy);
    check(dy > 0, "intensity 255 is what makes it travel down the panel");
  }
}

// --- controls ---------------------------------------------------------------

void test_control_defaults() {
  printf("Controls\n");

  // Sticky is the default, which is what a control pinned in YAML needs.
  {
    Engine engine;
    engine.init(16, 16);
    engine.set_effect("Scrolling Text");
    engine.set_intensity(255);
    engine.set_custom3(7);
    engine.set_effect("Matrix");
    check(engine.segment().intensity == 255, "a pinned intensity survives an effect change");
    check(engine.segment().custom3 == 7, "a pinned custom3 survives an effect change");
  }

  // With sticky off a control belongs to the effect it was set on.
  {
    Engine engine;
    engine.init(16, 16);
    engine.set_sticky_controls(false);
    engine.set_effect("Scrolling Text");
    engine.set_intensity(255);
    engine.set_custom3(7);
    check(engine.segment().intensity == 255, "the value still applies to the running effect");

    engine.set_effect("Matrix");
    const EffectDefaults matrix = effect_defaults(*EffectRegistry::find("Matrix"));
    check(engine.segment().intensity == matrix.intensity, "intensity is refilled from the new effect");
    check(engine.segment().custom3 == matrix.custom3, "custom3 is refilled from the new effect");

    engine.set_effect("Scrolling Text");
    const EffectDefaults text = effect_defaults(*EffectRegistry::find("Scrolling Text"));
    check(engine.segment().intensity == text.intensity, "and from this one when it comes back");
  }

  // custom3 is five bits everywhere: the setter clamps and no effect declares a
  // default outside the range the effects divide down.
  {
    Engine engine;
    engine.init(16, 16);
    engine.set_effect("Scrolling Text");
    engine.set_custom3(200);
    check(engine.segment().custom3 == 31, "set_custom3 clamps to 31");

    size_t out_of_range = 0;
    std::string names;
    for (size_t i = 0; i < EffectRegistry::count(); i++) {
      const EffectInfo *info = EffectRegistry::at(i);
      if (effect_defaults(*info).custom3 > 31) {
        char buffer[64];
        effect_name(*info, buffer, sizeof(buffer));
        names += std::string(names.empty() ? "" : ", ") + buffer;
        out_of_range++;
      }
    }
    check(out_of_range == 0,
          std::string("no effect declares a c3 default above 31") + (names.empty() ? std::string() : ": " + names));
  }
}

// --- control labels ---------------------------------------------------------

void test_labels() {
  printf("Control labels\n");
  struct Case {
    const char *effect;
    const char *controls;
    const char *colors;
  };
  // Read straight out of the WLED metadata these effects carry, so a change to
  // the parser that quietly drops a label shows up here.
  const Case cases[] = {
      {"Matrix", "Speed \xC2\xB7 Intensity: Spawning rate \xC2\xB7 Custom 1: Trail \xC2\xB7 Check 1: Custom color",
       "Color 1: Spawn \xC2\xB7 Color 2: Trail"},
      {"Solid", "Speed \xC2\xB7 Intensity",
       "Palette: Color palette \xC2\xB7 Color 1: Fx \xC2\xB7 Color 2: Bg \xC2\xB7 Color 3: Cs"},
  };
  for (const Case &c : cases) {
    const EffectInfo *info = EffectRegistry::find(c.effect);
    if (info == nullptr) {
      check(false, std::string(c.effect) + " is registered");
      continue;
    }
    char buffer[256];
    format_effect_controls(*info, buffer, sizeof(buffer));
    check(strcmp(buffer, c.controls) == 0, std::string(c.effect) + " controls: " + buffer);
    format_effect_colors(*info, buffer, sizeof(buffer));
    check(strcmp(buffer, c.colors) == 0, std::string(c.effect) + " colours: " + buffer);
  }

  // Every effect has to produce something a person can read, and it has to fit
  // in a state Home Assistant will accept.
  size_t longest = 0;
  for (size_t i = 0; i < EffectRegistry::count(); i++) {
    char buffer[512];
    const size_t a = format_effect_controls(*EffectRegistry::at(i), buffer, sizeof(buffer));
    const size_t b = format_effect_colors(*EffectRegistry::at(i), buffer, sizeof(buffer));
    longest = std::max(longest, std::max(a, b));
  }
  printf("      longest line: %u bytes\n", static_cast<unsigned>(longest));
  check(longest > 0 && longest <= 255, "every label line fits a Home Assistant state");
}

// --- gamma ------------------------------------------------------------------

// The brightest channel of a packed colour, ignoring white.
uint8_t peak_channel(uint32_t c) { return std::max(R(c), std::max(G(c), B(c))); }

/* Round 1 finding F1. The engine used to define gamma8, gamma8inv, gamma32 and
 * gamma32inv as identities on the grounds that gamma belongs to the output
 * stage. That is true of WLED's one gamma pass in show(), and false of the
 * gamma maths effects and the particle renderer do while drawing, which is
 * visible in WLED's own pre-output buffer. Twenty particle effects rendered
 * between 0.43 and 0.85 times WLED's brightness, and Pride 2015 at 0.59,
 * because of it.
 *
 * These are the assertions that would have caught it. */
void test_gamma() {
  printf("Gamma\n");

  // The tables are constants in wf_color.cpp; this is upstream's formula from
  // NeoGammaWLEDMethod::calcGammaTable(2.2), wled00/colors.cpp:652.
  int wrong = 0;
  for (int i = 1; i < 256; i++) {
    const int forward = static_cast<int>(powf(static_cast<float>(i) / 255.0f, 2.2f) * 255.0f + 0.5f);
    const int inverse = static_cast<int>(powf((static_cast<float>(i) - 0.5f) / 255.0f, 1.0f / 2.2f) * 255.0f + 0.5f);
    if (GAMMA_T[i] != forward || GAMMA_T_INV[i] != inverse)
      wrong++;
  }
  check(wrong == 0, "both gamma tables are WLED's, recomputed from powf at gamma 2.2");
  check(GAMMA_T[0] == 0 && GAMMA_T_INV[0] == 0, "both tables are zero at zero");
  check(gamma8(255) == 255 && gamma8inv(255) == 255, "both tables are unity at full scale");
  check(gamma8(128) == 56 && gamma8inv(128) == 186, "the tables are not identities");
  check(gamma32inv(RGBW32(64, 64, 64, 0)) == RGBW32(136, 136, 136, 0),
        "gamma32inv lifts a colour, which is what Pride 2015 rides on");

  /* The particle renderer's matched pair. A particle sitting exactly between
   * four pixels splits its brightness four ways; upstream then puts the inverse
   * gamma back on each quarter, so the four pixels come out at gamma8inv(63)
   * rather than at 63. Rendering the same particle centred on one pixel gives
   * the unsplit colour to compare against, so the check does not depend on
   * which colour the palette handed out. */
  {
    Engine engine;
    if (!engine.init(16, 16)) {
      check(false, "16x16 canvas for the particle renderer");
      return;
    }
    Segment &seg = engine.segment();
    // No effect is selected, so nothing has loaded the palette the renderer
    // colours particles from. Palette 0 builds it from the segment colours.
    seg.begin_draw(CRGBPalette16(CRGB(0xFFFFFF)));
    ParticleSystem2D *ps = nullptr;
    if (!initParticleSystem2D(seg, ps, 1) || ps == nullptr || ps->usedParticles < 1) {
      check(false, "2D particle system allocated");
      return;
    }
    ps->updateSystem();
    ps->setParticleSize(1);  // the 2x2 bilinear path, which is the one with the gamma pair and the 2D default
    ps->setMotionBlur(0);
    ps->setSmearBlur(0);
    for (uint32_t i = 0; i < ps->usedParticles; i++)
      ps->particles[i].ttl = 0;

    const auto place_and_render = [&](int32_t x, int32_t y) {
      ps->particles[0] = {static_cast<int16_t>(x), static_cast<int16_t>(y), 200, 0, 0, 0, 255};
      ps->particleFlags[0].outofbounds = false;
      ps->update();
    };

    // Dead centre of pixel (8, 8): the whole brightness lands on one pixel.
    place_and_render(8 * PS_P_RADIUS + PS_P_HALFRADIUS, 8 * PS_P_RADIUS + PS_P_HALFRADIUS);
    uint32_t whole = 0;
    size_t whole_lit = 0;
    for (size_t i = 0; i < engine.canvas().size(); i++) {
      const uint32_t c = engine.canvas().get(i) & 0x00FFFFFFu;
      if (c != 0) {
        whole_lit++;
        whole = std::max(whole, static_cast<uint32_t>(peak_channel(c)));
      }
    }
    check(whole_lit == 1 && whole > 0, "a particle on a pixel centre lights exactly that pixel");

    // On the corner between four pixels: each gets a quarter of the surface.
    place_and_render(8 * PS_P_RADIUS, 8 * PS_P_RADIUS);
    uint32_t quarter = 0;
    size_t quarter_lit = 0;
    for (size_t i = 0; i < engine.canvas().size(); i++) {
      const uint32_t c = engine.canvas().get(i) & 0x00FFFFFFu;
      if (c != 0) {
        quarter_lit++;
        quarter = std::max(quarter, static_cast<uint32_t>(peak_channel(c)));
      }
    }
    check(quarter_lit == 4, "a particle on a pixel corner lights four pixels");

    /* (64-32) * (64-32) * 255 >> 12 is 63, the bilinear weight, and
     * gamma8inv(63) is 135. Without the inverse gamma each pixel would carry 63
     * and this lands at 0.247 instead of 0.529. */
    const double ratio = whole > 0 ? static_cast<double>(quarter) / static_cast<double>(whole) : 0.0;
    const double expected = static_cast<double>(gamma8inv(63)) / 255.0;
    printf("      2D: one pixel %u, each of four %u, ratio %.3f, expected %.3f\n", whole, quarter, ratio, expected);
    check(std::fabs(ratio - expected) < 0.02, "the 2D renderer puts the inverse gamma back on the sub-pixel weights");
    seg.deallocate_data();
  }

  // The 1D renderer splits two ways rather than four, at PS_P_RADIUS_1D.
  {
    Engine engine;
    if (!engine.init(32, 1)) {
      check(false, "32x1 canvas for the 1D particle renderer");
      return;
    }
    Segment &seg = engine.segment();
    seg.begin_draw(CRGBPalette16(CRGB(0xFFFFFF)));
    ParticleSystem1D *ps = nullptr;
    if (!initParticleSystem1D(seg, ps, 1) || ps == nullptr || ps->usedParticles < 1) {
      check(false, "1D particle system allocated");
      return;
    }
    ps->updateSystem();
    // The 1D default is 0, single pixel and no interpolation. Size 1 is the two
    // pixel path, which is what every 1D particle effect that straddles pixels
    // selects, and the one carrying the gamma pair.
    ps->setParticleSize(1);
    ps->setMotionBlur(0);
    ps->setSmearBlur(0);
    for (uint32_t i = 0; i < ps->usedParticles; i++)
      ps->particles[i].ttl = 0;

    const auto place_and_render = [&](int32_t x) {
      ps->particles[0] = {x, 200, 0, 0};
      ps->particleFlags[0].outofbounds = false;
      ps->update();
    };

    place_and_render(16 * PS_P_RADIUS_1D + PS_P_HALFRADIUS_1D);
    uint32_t whole = 0;
    size_t whole_lit = 0;
    for (size_t i = 0; i < engine.canvas().size(); i++) {
      const uint32_t c = engine.canvas().get(i) & 0x00FFFFFFu;
      if (c != 0) {
        whole_lit++;
        whole = std::max(whole, static_cast<uint32_t>(peak_channel(c)));
      }
    }
    check(whole_lit == 1 && whole > 0, "a 1D particle on a pixel centre lights exactly that pixel");

    place_and_render(16 * PS_P_RADIUS_1D);
    uint32_t half = 0;
    size_t half_lit = 0;
    for (size_t i = 0; i < engine.canvas().size(); i++) {
      const uint32_t c = engine.canvas().get(i) & 0x00FFFFFFu;
      if (c != 0) {
        half_lit++;
        half = std::max(half, static_cast<uint32_t>(peak_channel(c)));
      }
    }
    check(half_lit == 2, "a 1D particle between two pixels lights both");
    const double ratio = whole > 0 ? static_cast<double>(half) / static_cast<double>(whole) : 0.0;
    const double expected = static_cast<double>(gamma8inv(127)) / 255.0;
    printf("      1D: one pixel %u, each of two %u, ratio %.3f, expected %.3f\n", whole, half, ratio, expected);
    check(std::fabs(ratio - expected) < 0.02, "the 1D renderer puts the inverse gamma back on the sub-pixel weights");
    seg.deallocate_data();
  }
}

// --- the effect scratch budget ----------------------------------------------

/* Round 1 finding F2. `strip.getMaxSegments()` was transformed to the literal 1,
 * which turned upstream's `segs <= (getMaxSegments() / 2)` into `1 <= 0`, so the
 * two doublings never fired and every effect that sizes a pool from the scratch
 * budget got a quarter of what WLED gives it. Fireworks Starburst rendered 34
 * stars where the reference device renders 68. */
void test_segment_data_budget() {
  printf("Effect scratch budget\n");

  check(strip_active_segments_num() == 1, "one canvas, one active segment");
  check(strip_max_segments() == MAX_NUM_SEGMENTS && MAX_NUM_SEGMENTS > 1,
        "getMaxSegments() is the build's segment limit, not the active count");
  check(strip_active_segments_num() <= strip_max_segments() / 4,
        "one segment out of many, so both of upstream's doublings fire");
  check(FAIR_DATA_PER_SEG == MAX_SEGMENT_DATA / MAX_NUM_SEGMENTS, "FAIR_DATA_PER_SEG is derived, not a magic number");

  /* The host build stands in for the reference hardware, an ESP32-S3 with
   * PSRAM: 64 segments out of a 64 KB budget is 1024 each, doubled twice is
   * 4096, and a star is 60 bytes. `leds.maxseg` from the device's own
   * /json/info says 64, which is where the 68 comes from. */
  const unsigned max_data = FAIR_DATA_PER_SEG * 4;
  printf("      FAIR_DATA_PER_SEG %u, budget %u bytes\n", FAIR_DATA_PER_SEG, max_data);
  check(max_data == 4096, "the host profile is the reference device's: 4096 bytes");

  Engine engine;
  if (!engine.init(64, 64)) {
    check(false, "64x64 canvas");
    return;
  }
  if (!engine.set_effect("Fireworks Starburst")) {
    check(false, "Fireworks Starburst is registered");
    return;
  }
  uint32_t now = 1000;
  run(engine, 2, now);
  const size_t stars = engine.segment().data_size() / 60;
  printf("      Fireworks Starburst: %u bytes, %u stars\n", static_cast<unsigned>(engine.segment().data_size()),
         static_cast<unsigned>(stars));
  check(stars == 68, "Fireworks Starburst gets the reference device's 68 stars, not the collapsed 34");

  if (engine.set_effect("Fireworks 1D")) {
    run(engine, 2, now);
    printf("      Fireworks 1D: %u bytes\n", static_cast<unsigned>(engine.segment().data_size()));
    check(engine.segment().data_size() > 2048,
          "Fireworks 1D's spark pool also grew past the undoubled FAIR_DATA_PER_SEG");
  }
}

// --- simulated sound --------------------------------------------------------

/* Round 1 finding F5. Puddlepeak, Ripple Peak and Waterfall draw only on
 * `sample_peak`, and the simulation fired it on about 2 percent of frames, so
 * all three were blank with no microphone. */
void test_simulated_peak() {
  printf("Simulated sound\n");

  // Every simulation mode has to produce a beat, because the three peak-driven
  // effects pin si=0 in their metadata and cannot pick another one.
  for (uint8_t mode = 0; mode < 4; mode++) {
    unsigned peaks = 0;
    // Four seconds at WLED's 23 ms frame, which is 174 frames.
    for (uint32_t now = 1000; now < 5000; now += 23)
      peaks += simulate_sound(mode, now).sample_peak ? 1 : 0;
    const double per_second = peaks / 4.0;
    printf("      si=%u: %.1f peaks per second\n", mode, per_second);
    check(per_second > 1.0 && per_second < 4.0, std::string("si=") + std::to_string(mode) + " beats at about 120 bpm");
  }

  // Frame rate must not change the beat rate: the simulator runs at 50 ms and
  // both front ends at 23 ms, and a peak counted per frame would differ.
  unsigned slow = 0;
  for (uint32_t now = 1000; now < 5000; now += 50)
    slow += simulate_sound(0, now).sample_peak ? 1 : 0;
  check(slow >= 7 && slow <= 9, "the beat is counted off the clock, not off the frame");

  // And the effects that need it are no longer blank.
  const char *peak_effects[] = {"Puddlepeak", "Ripple Peak", "Waterfall"};
  for (const char *name : peak_effects) {
    Engine engine;
    engine.init(64, 64);
    if (!engine.set_effect(name)) {
      check(false, std::string(name) + " is registered");
      continue;
    }
    uint32_t now = 1000;
    double total = 0.0;
    const int frames = 260;  // six seconds
    for (int i = 0; i < frames; i++) {
      engine.render(now);
      now += 23;
      for (size_t p = 0; p < engine.canvas().size(); p++)
        total += peak_channel(engine.canvas().get(p));
    }
    const double mean = total / (frames * static_cast<double>(engine.canvas().size()));
    printf("      %s mean brightness %.2f\n", name, mean);
    check(mean > 0.5, std::string(name) + " renders something with the simulated source");
  }
}

}  // namespace

int main() {
  test_scrolling_text();
  test_control_defaults();
  test_labels();
  test_gamma();
  test_segment_data_budget();
  test_simulated_peak();
  printf("\n%d failure(s)\n", failures);
  return failures;
}
