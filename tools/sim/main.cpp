// Host simulator for the wled_fx engine.
//
// Runs every registered effect at several geometries, checks the canvas guard
// bands after every frame, checks that something was actually drawn, and writes a
// PNG contact sheet per effect so the result can be looked at rather than just
// asserted.
//
// Usage:
//   wled_fx_sim [--effect NAME] [--group NAME] [--frames N] [--no-images]
//               [--out DIR] [--list] [--palette N] [--map N]

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
};

// Frames captured into the contact sheet.
const int CAPTURE_FRAMES[] = {1, 30, 90, 150, 210, 299};
constexpr int CAPTURE_COUNT = sizeof(CAPTURE_FRAMES) / sizeof(CAPTURE_FRAMES[0]);
constexpr int TILE_GAP = 4;

struct Result {
  std::string effect;
  std::string geometry;
  bool guards_ok{true};
  bool non_black{false};
  size_t max_lit{0};
  std::string image;
};

std::string sanitize(const std::string &name) {
  std::string out;
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    else if (!out.empty() && out.back() != '_')
      out += '_';
  }
  while (!out.empty() && out.back() == '_')
    out.pop_back();
  return out.empty() ? "effect" : out;
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
  int frames = 300;
  int palette = -1;  // -1 keeps the effect's own metadata default
  int map1d2d = -1;  // -1 keeps the effect's own m12 default
  bool images = true;
  bool list_only = false;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
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
    else if (arg == "--no-images")
      images = false;
    else if (arg == "--list")
      list_only = true;
    else {
      fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return 2;
    }
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

  if (selected.empty()) {
    fprintf(stderr, "no effects matched\n");
    return 1;
  }

  std::vector<Result> results;
  int failures = 0;

  for (const auto &entry : selected) {
    char name[64];
    effect_name(*entry.second, name, sizeof(name));

    for (const Geometry &geo : GEOMETRIES) {
      Result res;
      res.effect = name;
      res.geometry = geo.label;

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
      engine.set_text("WLED FX");
      if (palette >= 0)
        engine.set_palette(static_cast<uint8_t>(palette));
      if (map1d2d >= 0)
        engine.segment().map1d2d = static_cast<uint8_t>(map1d2d);

      const int scale_x = std::max(1, std::min(8, 128 / std::max<int>(1, geo.width)));
      const int scale_y = geo.height == 1 ? 16 : scale_x;
      const int tile_w = geo.width * scale_x;
      const int tile_h = geo.height * scale_y;
      const int sheet_w = CAPTURE_COUNT * tile_w + (CAPTURE_COUNT + 1) * TILE_GAP;
      const int sheet_h = tile_h + 2 * TILE_GAP;
      std::vector<uint8_t> sheet(static_cast<size_t>(sheet_w) * sheet_h * 3, 24);
      int captured = 0;

      uint32_t now = 1000;
      for (int f = 0; f < frames; f++) {
        engine.render(now);
        now += 23;  // WLED's nominal frame period

        if (!engine.canvas().guards_intact()) {
          res.guards_ok = false;
          fprintf(stderr, "FAIL %s %s: guard band overwritten at frame %d\n", name, geo.label, f);
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

        if (images && captured < CAPTURE_COUNT && f == CAPTURE_FRAMES[captured]) {
          blit_tile(sheet, sheet_w, TILE_GAP + captured * (tile_w + TILE_GAP), TILE_GAP, engine.canvas(), scale_x,
                    scale_y);
          captured++;
        }
      }

      if (!res.non_black && res.guards_ok) {
        fprintf(stderr, "FAIL %s %s: every frame was black\n", name, geo.label);
        failures++;
      }

      if (images && res.guards_ok) {
        res.image = out_dir + "/" + sanitize(name) + "_" + geo.label + ".png";
        if (!wfsim::write_png(res.image, sheet_w, sheet_h, sheet)) {
          fprintf(stderr, "FAIL %s %s: could not write %s\n", name, geo.label, res.image.c_str());
          failures++;
        }
      }

      results.push_back(res);
    }
  }

  printf("\n%-22s %-8s %-7s %-9s %s\n", "effect", "geom", "guards", "max lit", "image");
  printf("%s\n", std::string(72, '-').c_str());
  for (const Result &r : results) {
    printf("%-22s %-8s %-7s %-9zu %s\n", r.effect.c_str(), r.geometry.c_str(), r.guards_ok ? "ok" : "BAD", r.max_lit,
           r.image.c_str());
  }
  printf("\n%zu run(s), %d failure(s)\n", results.size(), failures);
  return failures == 0 ? 0 : 1;
}
