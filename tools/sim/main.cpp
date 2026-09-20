// Host simulator for the wled_fx engine.
//
// Runs every registered effect at several geometries, checks the canvas guard
// bands after every frame, checks that something was actually drawn, and writes a
// PNG contact sheet per effect so the result can be looked at rather than just
// asserted.
//
// Usage:
//   wled_fx_sim [--effect NAME] [--group NAME] [--frames N] [--no-images]
//               [--out DIR] [--list] [--list-meta] [--palette N] [--map N]
//               [--size WxH] [--check1 0|1] [--check2 0|1] [--check3 0|1]
//               [--checks-on] [--custom1 N] [--custom2 N] [--custom3 N]
//               [--speed N] [--intensity N] [--text STRING]
//               [--single-pass] [--anim DIR] [--color1 RRGGBB] [--color2 ..] [--color3 ..]
//               [--step-ms N]
//
// With none of the control options, every effect is run twice per geometry: once
// on its own metadata defaults and once with all three checkmarks on. The second
// pass is the only thing that reaches the alternative modes a lot of effects hide
// behind a checkbox (PS Pinball's rolling and collide, PS Springy's AR mode, the
// Cylinder and Collide options on most of the particle effects). Naming any
// control on the command line replaces both passes with that one configuration,
// and --single-pass drops the checks pass while leaving the defaults alone.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../components/wled_fx/wf_engine.h"
#include "../../components/wled_fx/wf_registry.h"
#include "png_writer.h"

using namespace esphome::wled_fx;

namespace {

struct Geometry {
  const char *label;
  uint16_t width;
  uint16_t height;
};

const Geometry GEOMETRIES[] = {
    {"16x16", 16, 16},
    {"64x64", 64, 64},
    {"60x1", 60, 1},
    // Non-square matrices. Width and height are swapped between the first two so
    // that an effect that mixes up its axes fails on one of them, and 31x17 is odd
    // in both dimensions so nothing can quietly rely on a power of two.
    {"64x32", 64, 32},
    {"32x64", 32, 64},
    {"31x17", 31, 17},
};

// The default run, in frames and in milliseconds of effect time per frame.
constexpr int DEFAULT_FRAMES = 300;
constexpr uint32_t DEFAULT_STEP_MS = 23;  // WLED's nominal frame period

/* Frames captured into the contact sheet, given for the default 300 frame run
 * and scaled to whatever the run actually is, so an effect with a longer run
 * still gets its last tile near the end rather than six tiles from the opening
 * seconds. */
const int CAPTURE_FRAMES[] = {1, 30, 90, 150, 210, 299};
constexpr int CAPTURE_COUNT = sizeof(CAPTURE_FRAMES) / sizeof(CAPTURE_FRAMES[0]);
constexpr int TILE_GAP = 4;

/* --anim writes raw RGB24 frames instead of a contact sheet, for the animated
 * gallery previews: 100 frames at 50 ms is five seconds of effect time played
 * back at 20 fps. The PACING table still applies, because its time acceleration
 * is the only way a slow effect shows anything: a longer frame period is scaled
 * by the same ratio it has in the default run, and an effect that needs a few
 * hundred frames to build its picture gets them as a warm-up that is rendered
 * and not written. The short warm-up every effect gets skips the opening frames,
 * where a lot of effects are still fading up from black. */
constexpr int ANIM_FRAMES = 100;
constexpr uint32_t ANIM_STEP_MS = 50;
constexpr int ANIM_WARMUP = 20;

int capture_frame(int index, int effect_frames) {
  if (effect_frames == DEFAULT_FRAMES)
    return CAPTURE_FRAMES[index];
  const long scaled = static_cast<long>(CAPTURE_FRAMES[index]) * effect_frames / DEFAULT_FRAMES;
  return static_cast<int>(std::min<long>(scaled, effect_frames - 1));
}

/* A few effects are paced against wall-clock time so slowly that 300 frames at
 * 23 ms, about seven seconds, never reaches anything visible. Rather than let
 * them fail the non-black check, or run them for the hundreds of thousands of
 * frames real time would need, the simulator hands those effects a longer frame
 * period. Nothing in the effect changes: it still only reads `seg.now`, so this
 * is time acceleration, not a special case that lets an effect off. */
struct Pacing {
  const char *effect;
  int frames;
  uint32_t step_ms;
};

const Pacing PACING[] = {
    // Sunrise's default speed is a 60 minute sunrise, and its palette lookup stays
    // black for the first quarter of it. 300 frames at 12 s covers an hour.
    {"Sunrise", DEFAULT_FRAMES, 12000},
    /* PS Galaxy spirals its particles out from the centre a little each frame, so
     * for the first few hundred frames it is a bright blob and the arms are not
     * there yet. This one needs real frames rather than a longer frame period,
     * because the motion is per-frame and not clock driven. */
    {"PS Galaxy", 1500, DEFAULT_STEP_MS},
};

Pacing pacing_for(const char *name) {
  for (const Pacing &p : PACING) {
    if (strcmp(p.effect, name) == 0)
      return p;
  }
  return Pacing{name, DEFAULT_FRAMES, DEFAULT_STEP_MS};
}

/* One configuration of the six controls that are not speed, intensity or
 * palette. -1 means "leave the effect's own metadata default alone"; anything
 * else is pinned on the engine, which also keeps it across an effect change. */
struct Controls {
  const char *label{""};  // suffixed onto the PNG name, empty for the default pass
  int check[3]{-1, -1, -1};
  int custom[3]{-1, -1, -1};

  bool any_set() const {
    for (int i = 0; i < 3; i++) {
      if (check[i] >= 0 || custom[i] >= 0)
        return true;
    }
    return false;
  }

  void apply(Engine &engine) const {
    if (check[0] >= 0)
      engine.set_check1(check[0] != 0);
    if (check[1] >= 0)
      engine.set_check2(check[1] != 0);
    if (check[2] >= 0)
      engine.set_check3(check[2] != 0);
    if (custom[0] >= 0)
      engine.set_custom1(static_cast<uint8_t>(custom[0]));
    if (custom[1] >= 0)
      engine.set_custom2(static_cast<uint8_t>(custom[1]));
    if (custom[2] >= 0)
      engine.set_custom3(static_cast<uint8_t>(custom[2]));
  }
};

/* Effects that correctly render nothing on a short enough strip, so that the
 * non-black assertion does not turn a faithful port into a failure. The length
 * bound keeps the assertion live everywhere else, which is the point: a
 * regression at a normal size still fails.
 *
 * PS Sonic Boom emits `hw_random16(((4 + (maxXpixel >> 2)) * loudness) >> 10)`
 * particles per detected beat. At the metadata default of c3=0 the bin value is 0
 * to 255, so the inner expression only reaches 2 once `4 + (maxXpixel >> 2)` is
 * 9, which needs 21 pixels; below that it is 1 and hw_random16(1) is always 0, so
 * no particle is ever emitted. c3 >= 26 picks a bin above 12, which the effect
 * shifts left by 2 to a loudness of up to 1020, and then it emits at any length:
 * `--effect "PS Sonic Boom" --size 16x1 --custom3 27` is the way to confirm the
 * rendering path is healthy at these lengths and only the emission arithmetic is
 * rounding to nothing. Neither of the two default passes moves c3. That is
 * upstream's arithmetic unchanged (WLED FX.cpp mode_particle1DsonicBoom), and it
 * bites here because a 1D effect pushed through M12_P_BAR or M12_P_CORNER on a
 * 16x16 matrix gets a 16 pixel virtual strip.
 *
 * The short strip entries below are the same class of thing at the other end: an
 * effect that divides a strip into regions and has none left at two or three
 * pixels. Each bound is the largest length at which that effect is still black,
 * so the assertion stays live at every size anyone would actually wire up. */
struct BlackAllowed {
  const char *effect;
  unsigned max_length;  // forgiven only at or below this virtual strip length
};

const BlackAllowed BLACK_ALLOWED[] = {
    {"PS Sonic Boom", 20},
    // Scanner fades the whole strip to the secondary colour and then paints one
    // moving pixel, and at four pixels or fewer its frames-per-pixel pacing means
    // the painted pixel is fading out faster than it moves. Scanner Dual is
    // Scanner with check1 forced on. Upstream FX.cpp mode_larson_scanner.
    {"Scanner", 3},
    {"Scanner Dual", 3},
    // `for (i = 0; i < SEGLEN - 2; i += 3)`: at two pixels there is no light to
    // paint, and the background it filled first is the secondary colour, black by
    // default. Upstream FX.cpp mode_traffic_light.
    {"Traffic Light", 2},
    // A starburst needs somewhere to throw its fragments. Upstream FX.cpp
    // mode_starburst, and the MM audio variant of the same body.
    {"Fireworks Starburst", 2},
    /* The audio variant gets one more pixel of slack than the plain one, and
     * for a different reason. Below four pixels `numStars` is 1, and a single
     * star is born on `hw_random8(birthrate) == 0` with a birthrate up to 144,
     * so whether anything happens in a 300 frame run is a coin toss: this
     * effect lights up at 3 pixels with 900 frames and at 4 pixels with 300.
     * It sat on the winning side of that toss until the simulated sound
     * stopped drawing a random number per frame and moved the shared sequence
     * along. Not a size at which it is structurally black, which is why the
     * bound is 3 and not higher. */
    {"Fw Starburst audio", 3},
    // `ledIndex = (prog * SEGLEN * 3) >> 16` with the three colour bands all at
    // the same pixel. Upstream FX.cpp mode_tricolor_wipe.
    {"Tri Wipe", 1},
    // `color_sep = 256 / SEGLEN` then a chase across a strip with nothing to
    // chase along. Upstream FX.cpp mode_chase_rainbow.
    {"Chase Rainbow", 1},
};

bool black_allowed(const char *name, unsigned seg_length) {
  for (const BlackAllowed &b : BLACK_ALLOWED) {
    if (strcmp(b.effect, name) == 0 && seg_length <= b.max_length)
      return true;
  }
  return false;
}

struct Result {
  std::string effect;
  std::string geometry;
  std::string controls;
  bool guards_ok{true};
  bool non_black{false};
  size_t max_lit{0};
  std::string image;
};

/* Punctuation that carries meaning in an effect name and so has to survive into
 * a derived identifier. Collapsing it all to "_" made "Sparkle" and "Sparkle+"
 * the same macro and the same PNG file name. Keep this table in step with
 * _NAME_TOKENS in components/wled_fx/__init__.py. */
const char *effect_name_token(char c) {
  switch (c) {
    case '+':
      return "_PLUS";
    case '&':
      return "_AND";
    case '/':
      return "_SLASH";
    case '#':
      return "_HASH";
    case '%':
      return "_PCT";
    case '*':
      return "_STAR";
    default:
      return nullptr;
  }
}

// The identifier a YAML allow-list entry turns into, matching effect_macro() in
// components/wled_fx/__init__.py.
std::string effect_macro(const std::string &name) {
  std::string expanded;
  for (char c : name) {
    const char *token = effect_name_token(c);
    if (token != nullptr)
      expanded += token;
    else
      expanded += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
  }
  std::string out;
  for (char c : expanded) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      out += c;
    else if (!out.empty() && out.back() != '_')
      out += '_';
  }
  while (!out.empty() && out.back() == '_')
    out.pop_back();
  return "WLED_FX_FX_" + out;
}

// The stem of this effect's PNG file names. Same rule as effect_macro(), lower
// cased, so two effects collide in one place or in neither.
std::string sanitize(const std::string &name) {
  std::string out = effect_macro(name).substr(strlen("WLED_FX_FX_"));
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + 32);
  }
  return out.empty() ? "effect" : out;
}

/* Two effects whose names derive to the same macro would share a YAML
 * allow-list entry, and two that derive to the same file stem would overwrite
 * each other's contact sheets. Both were real: "Sparkle" and "Sparkle+". This
 * runs on every simulator invocation, so CI cannot miss a new one. */
bool check_derived_names(const std::vector<std::pair<std::string, const EffectInfo *>> &selected) {
  std::vector<std::pair<std::string, std::string>> macros;  // derived, effect
  std::vector<std::pair<std::string, std::string>> stems;
  int clashes = 0;

  for (const auto &entry : selected) {
    char name[64];
    effect_name(*entry.second, name, sizeof(name));
    for (auto *table : {&macros, &stems}) {
      const bool is_macro = (table == &macros);
      const std::string derived = is_macro ? effect_macro(name) : sanitize(name);
      for (const auto &seen : *table) {
        if (seen.first == derived) {
          fprintf(stderr, "FAIL name collision: \"%s\" and \"%s\" both derive %s \"%s\"\n", seen.second.c_str(), name,
                  is_macro ? "the macro" : "the output file name", derived.c_str());
          clashes++;
        }
      }
      table->emplace_back(derived, name);
    }
  }
  return clashes == 0;
}

void blit_tile(std::vector<uint8_t> &sheet, int sheet_w, int ox, int oy, const Canvas &canvas, int scale_x,
               int scale_y) {
  for (int y = 0; y < canvas.height(); y++) {
    for (int x = 0; x < canvas.width(); x++) {
      const uint32_t c = canvas.pixels()[x + y * canvas.width()];
      const uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
      for (int sy = 0; sy < scale_y; sy++) {
        for (int sx = 0; sx < scale_x; sx++) {
          const int px = ox + x * scale_x + sx;
          const int py = oy + y * scale_y + sy;
          const size_t idx = (static_cast<size_t>(py) * sheet_w + px) * 3;
          sheet[idx] = r;
          sheet[idx + 1] = g;
          sheet[idx + 2] = b;
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  std::string only_effect;
  std::string only_group;
  std::string out_dir = "out";
  int frames = 0;  // 0 keeps the per-effect default; --frames pins every effect
  int palette = -1;  // -1 keeps the effect's own metadata default
  int map1d2d = -1;  // -1 keeps the effect's own m12 default
  bool images = true;
  bool list_only = false;
  bool list_meta = false;
  std::string anim_dir;  // non-empty switches the run over to raw frame output
  /* --step-ms pins the frame period for every effect and turns the PACING
   * table off, including its warm-up. A comparison run against a real device
   * needs both: the device renders at about 23 ms and gives no effect a head
   * start, and an engine capture on a 50 ms clock with PS Galaxy 1500 frames
   * ahead is not comparable with it. 0 leaves the pacing alone. */
  uint32_t step_ms_override = 0;
  // --size WxH replaces the three default geometries with one of your own, for
  // checking a geometry the defaults do not cover (128x64, 300x1, ...).
  std::string size_label;
  std::vector<Geometry> geometries(GEOMETRIES, GEOMETRIES + sizeof(GEOMETRIES) / sizeof(GEOMETRIES[0]));
  Controls cli;
  bool single_pass = false;
  // -1 keeps the effect's own metadata default, as the other controls do.
  int speed = -1;
  int intensity = -1;
  /* The three WLED colour slots, -1 for "leave the engine's own default". They
   * are here so a run can be matched against a device that was captured with
   * different colours: WLED's own default is amber and black and black, and an
   * effect that paints the primary colour directly reports a different hue on
   * any other. */
  long colors[3] = {-1, -1, -1};
  // What the text effects render. The hardware test firmwares use this string.
  std::string text = "WLED FX";

  // --check<n> / --custom<n>, handled together because they differ only in range.
  const auto control_arg = [&](const std::string &arg, const char *prefix, int *slots, int hi, int *next) -> int {
    const size_t plen = strlen(prefix);
    if (arg.compare(0, plen, prefix) != 0 || arg.size() != plen + 1)
      return 0;  // not this option
    const int n = arg[plen] - '1';
    if (n < 0 || n > 2 || next == nullptr)
      return -1;
    if (*next < 0 || *next > hi) {
      fprintf(stderr, "%s wants a value from 0 to %d\n", arg.c_str(), hi);
      return -1;
    }
    slots[n] = *next;
    return 1;
  };

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg.compare(0, 7, "--check") == 0 && arg != "--checks-on") {
      int value = i + 1 < argc ? atoi(argv[i + 1]) : -1;
      const int r = control_arg(arg, "--check", cli.check, 1, &value);
      if (r < 0)
        return 2;
      if (r > 0) {
        i++;
        continue;
      }
    }
    if (arg.compare(0, 8, "--custom") == 0) {
      int value = i + 1 < argc ? atoi(argv[i + 1]) : -1;
      const int r = control_arg(arg, "--custom", cli.custom, 255, &value);
      if (r < 0)
        return 2;
      if (r > 0) {
        i++;
        continue;
      }
    }
    if (arg == "--checks-on") {
      cli.check[0] = cli.check[1] = cli.check[2] = 1;
      continue;
    }
    if (arg == "--single-pass") {
      single_pass = true;
      continue;
    }
    if (arg == "--effect" && i + 1 < argc)
      only_effect = argv[++i];
    else if (arg == "--group" && i + 1 < argc)
      only_group = argv[++i];
    else if (arg == "--out" && i + 1 < argc)
      out_dir = argv[++i];
    else if (arg == "--frames" && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (arg == "--palette" && i + 1 < argc)
      palette = atoi(argv[++i]);
    else if (arg == "--map" && i + 1 < argc)
      map1d2d = atoi(argv[++i]);
    else if (arg == "--speed" && i + 1 < argc)
      speed = atoi(argv[++i]);
    else if (arg == "--intensity" && i + 1 < argc)
      intensity = atoi(argv[++i]);
    else if (arg == "--text" && i + 1 < argc)
      text = argv[++i];
    else if ((arg == "--color1" || arg == "--color2" || arg == "--color3") && i + 1 < argc)
      colors[arg[7] - '1'] = strtol(argv[++i], nullptr, 16);  // RRGGBB
    else if (arg == "--size" && i + 1 < argc) {
      size_label = argv[++i];
      int w = 0, h = 0;
      if (sscanf(size_label.c_str(), "%dx%d", &w, &h) != 2 || w < 1 || h < 1) {
        fprintf(stderr, "--size wants WxH, for example 128x64\n");
        return 2;
      }
      geometries.assign(1, Geometry{size_label.c_str(), static_cast<uint16_t>(w), static_cast<uint16_t>(h)});
    } else if (arg == "--anim" && i + 1 < argc)
      anim_dir = argv[++i];
    else if (arg == "--step-ms" && i + 1 < argc)
      step_ms_override = static_cast<uint32_t>(atoi(argv[++i]));
    else if (arg == "--no-images")
      images = false;
    else if (arg == "--list")
      list_only = true;
    else if (arg == "--list-meta")
      list_meta = true;
    else {
      fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return 2;
    }
  }

  /* An animation run is one configuration of one geometry: contact sheets and
   * the checks pass both belong to the verification run, not to a preview. */
  const bool anim = !anim_dir.empty();
  if (anim) {
    images = false;
    single_pass = true;
  }

  // Collect the effects to run, keeping the group each one came from.
  std::vector<std::pair<std::string, const EffectInfo *>> selected;
  for (size_t gi = 0; gi < EffectRegistry::group_count(); gi++) {
    const EffectGroup &group = EffectRegistry::group(gi);
    if (!only_group.empty() && only_group != group.group_name)
      continue;
    for (size_t i = 0; i < group.count; i++) {
      char name[64];
      effect_name(group.entries[i], name, sizeof(name));
      if (!only_effect.empty() && only_effect != name)
        continue;
      selected.emplace_back(group.group_name, &group.entries[i]);
    }
  }

  if (list_only) {
    for (const auto &entry : selected) {
      char name[64];
      effect_name(*entry.second, name, sizeof(name));
      const EffectDefaults d = effect_defaults(*entry.second);
      printf("%-10s %-20s flags=0x%02X pal=%u sx=%u ix=%u m12=%u\n", entry.first.c_str(), name, d.flags, d.palette,
             d.speed, d.intensity, d.map1d2d);
    }
    printf("%zu effect(s)\n", selected.size());
    return 0;
  }

  /* The metadata string verbatim, one effect per line, so a tool that wants the
   * slider and checkmark labels reads them from the registry rather than
   * re-deriving them from the sources. */
  if (list_meta) {
    for (const auto &entry : selected)
      printf("%s\t%s\n", entry.first.c_str(), entry.second->metadata);
    return 0;
  }

  if (selected.empty()) {
    fprintf(stderr, "no effects matched\n");
    return 1;
  }

  /* Two passes per effect by default. Nearly every effect hides a second mode
   * behind a checkmark and the metadata defaults leave most of them off, so the
   * first pass alone never enters that code: PS Pinball's rolling and collide
   * modes, PS Springy's AR mode, the Cylinder, Collide and Gravity options across
   * the particle effects were all unexercised until this existed. Pinning any
   * control on the command line collapses this to the one configuration asked
   * for, so --effect X --check1 1 stays a single run. */
  std::vector<Controls> passes;
  if (cli.any_set()) {
    // Suffixed too, so an experiment cannot silently overwrite the contact sheet
    // the default run wrote for the same effect and geometry.
    cli.label = "cli";
    passes.push_back(cli);
  } else {
    passes.push_back(Controls{});
    if (!single_pass)
      passes.push_back(Controls{"checks", {1, 1, 1}, {-1, -1, -1}});
  }

  std::vector<Result> results;
  int failures = 0;

  // Checked against every registered effect, not just the selection, so that
  // --effect or --group cannot hide a clash.
  std::vector<std::pair<std::string, const EffectInfo *>> everything;
  for (size_t gi = 0; gi < EffectRegistry::group_count(); gi++) {
    const EffectGroup &group = EffectRegistry::group(gi);
    for (size_t i = 0; i < group.count; i++)
      everything.emplace_back(group.group_name, &group.entries[i]);
  }
  if (!check_derived_names(everything))
    failures++;

  for (const auto &entry : selected) {
    char name[64];
    effect_name(*entry.second, name, sizeof(name));
    const Pacing pacing = pacing_for(name);
    const bool paced = step_ms_override == 0;
    const int effect_frames =
        frames > 0 ? frames : (anim ? ANIM_FRAMES : (paced ? pacing.frames : DEFAULT_FRAMES));
    // Same time acceleration as the default run, at the preview's frame period.
    const uint32_t step_ms =
        !paced ? step_ms_override
               : (anim ? std::max<uint32_t>(1, pacing.step_ms * ANIM_STEP_MS / DEFAULT_STEP_MS) : pacing.step_ms);
    // Rendered and thrown away, so a per-frame paced effect has built its picture
    // by the first written frame.
    const int warmup = (anim && paced) ? ANIM_WARMUP + std::max(0, pacing.frames - DEFAULT_FRAMES) : 0;

    for (const Geometry &geo : geometries) {
      for (const Controls &controls : passes) {
        Result res;
        res.effect = name;
        res.geometry = geo.label;
        res.controls = controls.label;

        platform_seed_random(0xC0FFEEu);

        Engine engine;
        if (!engine.init(geo.width, geo.height)) {
          fprintf(stderr, "FAIL %s %s: canvas allocation failed\n", name, geo.label);
          failures++;
          continue;
        }
        if (!engine.set_effect(name)) {
          fprintf(stderr, "FAIL %s: not registered\n", name);
          failures++;
          continue;
        }
        engine.set_text(text.c_str());
        if (palette >= 0)
          engine.set_palette(static_cast<uint8_t>(palette));
        if (speed >= 0)
          engine.set_speed(static_cast<uint8_t>(speed));
        if (intensity >= 0)
          engine.set_intensity(static_cast<uint8_t>(intensity));
        // After set_effect(), which does not touch the colours, exactly as
        // WLED's fxdef does not.
        for (int slot = 0; slot < 3; slot++) {
          if (colors[slot] >= 0)
            engine.segment().colors[slot] = static_cast<uint32_t>(colors[slot]);
        }
        /* m12 is the *1D to 2D* mapping, so it only means anything for an effect
         * that can run in 1D. WLED only offers the "Expand 1D FX" selector on those
         * effects, and forcing a mapping onto a 2D-native effect breaks the same
         * assumption upstream makes: Game Of Life sizes its allocation from
         * seg.length(), which under M12_P_BAR is the matrix height rather than
         * width * height. Leave a 2D-only effect on its own default. */
        if (map1d2d >= 0 && (effect_defaults(*entry.second).flags & EFFECT_FLAG_1D) != 0)
          engine.segment().map1d2d = static_cast<uint8_t>(map1d2d);
        controls.apply(engine);

        const int scale_x = std::max(1, std::min(8, 128 / std::max<int>(1, geo.width)));
        const int scale_y = geo.height == 1 ? 16 : scale_x;
        const int tile_w = geo.width * scale_x;
        const int tile_h = geo.height * scale_y;
        const int sheet_w = CAPTURE_COUNT * tile_w + (CAPTURE_COUNT + 1) * TILE_GAP;
        const int sheet_h = tile_h + 2 * TILE_GAP;
        std::vector<uint8_t> sheet(static_cast<size_t>(sheet_w) * sheet_h * 3, 24);
        int captured = 0;

        FILE *anim_fp = nullptr;
        std::vector<uint8_t> anim_row;
        if (anim) {
          // One geometry is the normal case, because a preview is rendered with
          // --size. Several would otherwise write one file over and over.
          res.image = anim_dir + "/" + sanitize(name);
          if (geometries.size() > 1)
            res.image += std::string("_") + geo.label;
          res.image += ".rgb";
          anim_fp = fopen(res.image.c_str(), "wb");
          if (anim_fp == nullptr) {
            fprintf(stderr, "FAIL %s %s: could not open %s\n", name, geo.label, res.image.c_str());
            failures++;
            continue;
          }
          anim_row.resize(static_cast<size_t>(geo.width) * geo.height * 3);
        }

        uint32_t now = 1000;
        for (int f = 0; f < warmup + effect_frames; f++) {
          engine.render(now);
          now += step_ms;

          if (!engine.canvas().guards_intact()) {
            res.guards_ok = false;
            fprintf(stderr, "FAIL %s %s%s%s: guard band overwritten at frame %d\n", name, geo.label,
                    *controls.label ? " " : "", controls.label, f);
            failures++;
            break;
          }

          size_t lit = 0;
          for (size_t i = 0; i < engine.canvas().size(); i++) {
            if ((engine.canvas().get(i) & 0x00FFFFFFu) != 0)
              lit++;
          }
          res.max_lit = std::max(res.max_lit, lit);
          if (lit > 0)
            res.non_black = true;

          if (images && captured < CAPTURE_COUNT && f == capture_frame(captured, effect_frames)) {
            blit_tile(sheet, sheet_w, TILE_GAP + captured * (tile_w + TILE_GAP), TILE_GAP, engine.canvas(), scale_x,
                      scale_y);
            captured++;
          }

          if (anim_fp != nullptr && f >= warmup) {
            for (size_t i = 0; i < engine.canvas().size(); i++) {
              const uint32_t c = engine.canvas().get(i);
              anim_row[i * 3] = (c >> 16) & 0xFF;
              anim_row[i * 3 + 1] = (c >> 8) & 0xFF;
              anim_row[i * 3 + 2] = c & 0xFF;
            }
            fwrite(anim_row.data(), 1, anim_row.size(), anim_fp);
          }
        }

        if (anim_fp != nullptr) {
          fclose(anim_fp);
          // The manifest the gallery build reads: one line per written animation.
          printf("anim\t%s\t%u\t%u\t%d\t%s\n", res.image.c_str(), geo.width, geo.height, effect_frames, name);
        }

        /* "Something was drawn" is only a fair assertion on the effect's own
         * defaults. Several effects put an Overlay checkmark on check2, which tells
         * them not to paint a background at all and to composite onto whatever is
         * already on the strip, and the secondary colour defaults to black, so an
         * all-black frame is the correct result: Sparkle Dark, Sparkle+ and Snow
         * Fall all do this. The checks pass is there for the guard bands and for
         * reaching the code, so a black result is reported and not failed. */
        if (!res.non_black && res.guards_ok) {
          /* An animation run is a preview at one geometry and one pacing, not the
           * verification sweep, so a black result there is something for a human to
           * look at rather than a failure: the sweep is where it is a failure. */
          if (anim) {
            fprintf(stderr, "note %s %s: every frame of the animation was black\n", name, geo.label);
          } else if (controls.label[0] != '\0') {
            fprintf(stderr, "note %s %s %s: every frame was black\n", name, geo.label, controls.label);
          } else if (black_allowed(name, engine.segment().length())) {
            fprintf(stderr, "note %s %s: every frame was black, allowed at %u pixels\n", name, geo.label,
                    engine.segment().length());
          } else {
            fprintf(stderr, "FAIL %s %s: every frame was black\n", name, geo.label);
            failures++;
          }
        }

        if (images && res.guards_ok) {
          res.image = out_dir + "/" + sanitize(name) + "_" + geo.label;
          if (*controls.label != '\0')
            res.image += std::string("_") + controls.label;
          res.image += ".png";
          if (!wfsim::write_png(res.image, sheet_w, sheet_h, sheet)) {
            fprintf(stderr, "FAIL %s %s: could not write %s\n", name, geo.label, res.image.c_str());
            failures++;
          }
        }

        results.push_back(res);
      }
    }
  }

  printf("\n%-22s %-8s %-7s %-7s %-9s %s\n", "effect", "geom", "ctrl", "guards", "max lit", "image");
  printf("%s\n", std::string(80, '-').c_str());
  for (const Result &r : results) {
    printf("%-22s %-8s %-7s %-7s %-9zu %s\n", r.effect.c_str(), r.geometry.c_str(),
           r.controls.empty() ? "-" : r.controls.c_str(), r.guards_ok ? "ok" : "BAD", r.max_lit, r.image.c_str());
  }
  printf("\n%zu run(s), %d failure(s)\n", results.size(), failures);
  return failures == 0 ? 0 : 1;
}
