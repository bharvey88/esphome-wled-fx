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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../components/wled_fx/wf_engine.h"
#include "../../components/wled_fx/wf_math.h"
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

}  // namespace

int main() {
  test_scrolling_text();
  test_control_defaults();
  test_labels();
  printf("\n%d failure(s)\n", failures);
  return failures;
}
