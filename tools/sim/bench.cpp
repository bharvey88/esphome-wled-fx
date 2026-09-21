// Host benchmark and golden-frame check for the wled_fx engine.
//
// Two jobs, because they want the same run:
//
//   --bench   renders every effect for a fixed number of frames at a fixed
//             frame period and prints microseconds per frame. Comparing two
//             builds of this, for instance -Os against -O2, is the only
//             measurement of an engine change that can be made without the
//             hardware.
//
//   --check   renders the same frames and compares a hash of the canvas after
//             each one against tools/sim/golden.txt. An optimisation that
//             changes a single pixel of a single frame of a single effect
//             fails here, which is what makes "faster" safe to claim.
//
// Both are deterministic: the host RNG is reseeded per effect, the frame clock
// steps in fixed jumps and nothing reads wall time. The same binary on the same
// sources gives the same hashes on any machine.
//
// Usage:
//   wled_fx_bench [--bench] [--check FILE] [--write FILE] [--effect NAME]
//                 [--frames N] [--size WxH] [--repeats N]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../components/wled_fx/wf_engine.h"
#include "../../components/wled_fx/wf_registry.h"

using namespace esphome::wled_fx;

namespace {

constexpr int DEFAULT_FRAMES = 40;
constexpr uint32_t STEP_MS = 23;      // WLED's nominal frame period
constexpr uint32_t START_MS = 10000;  // away from zero, so nothing sees a first-boot special case
constexpr uint32_t SEED = 0xC0FFEE11u;

// FNV-1a over the whole canvas, folded once per frame so the hash covers the
// sequence and not only the last picture.
uint64_t hash_canvas(uint64_t running, const Canvas &canvas) {
  const uint32_t *pixels = canvas.pixels();
  const size_t count = canvas.size();
  for (size_t i = 0; i < count; i++) {
    running ^= pixels[i];
    running *= 1099511628211ull;
  }
  return running;
}

struct Result {
  std::string name;
  uint64_t hash;
  double us_per_frame;
};

Result run_effect(size_t index, uint16_t width, uint16_t height, int frames, int repeats) {
  char name[64];
  effect_name(*EffectRegistry::at(index), name, sizeof(name));

  Result result;
  result.name = name;
  result.hash = 0;
  result.us_per_frame = 0.0;

  double best = 0.0;
  for (int repeat = 0; repeat < repeats; repeat++) {
    Engine engine;
    if (!engine.init(width, height)) {
      fprintf(stderr, "bench: canvas allocation failed for %ux%u\n", width, height);
      exit(2);
    }
    platform_seed_random(SEED);
    engine.set_effect_index(index);

    uint64_t running = 1469598103934665603ull;
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; frame++) {
      engine.render(START_MS + static_cast<uint32_t>(frame) * STEP_MS);
      running = hash_canvas(running, engine.canvas());
    }
    const auto ended = std::chrono::steady_clock::now();
    const double us =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started).count() / 1000.0 / frames;
    if (repeat == 0 || us < best)
      best = us;
    result.hash = running;
  }
  // The fastest repeat, not the mean: the slow ones are the machine doing
  // something else, and this is a comparison between two builds of the same
  // work, not a claim about absolute speed.
  result.us_per_frame = best;
  return result;
}

bool read_golden(const char *path, std::map<std::string, uint64_t> &into) {
  FILE *file = fopen(path, "r");
  if (file == nullptr)
    return false;
  char line[256];
  while (fgets(line, sizeof(line), file) != nullptr) {
    if (line[0] == '#' || line[0] == '\n')
      continue;
    char *tab = strchr(line, '\t');
    if (tab == nullptr)
      continue;
    *tab = '\0';
    into[line] = strtoull(tab + 1, nullptr, 16);
  }
  fclose(file);
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  bool bench = false;
  const char *check_path = nullptr;
  const char *write_path = nullptr;
  const char *only = nullptr;
  int frames = DEFAULT_FRAMES;
  int repeats = 1;
  uint16_t width = 64;
  uint16_t height = 64;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    auto next = [&]() -> const char * {
      if (i + 1 >= argc) {
        fprintf(stderr, "bench: %s needs a value\n", arg);
        exit(2);
      }
      return argv[++i];
    };
    if (strcmp(arg, "--bench") == 0)
      bench = true;
    else if (strcmp(arg, "--check") == 0)
      check_path = next();
    else if (strcmp(arg, "--write") == 0)
      write_path = next();
    else if (strcmp(arg, "--effect") == 0)
      only = next();
    else if (strcmp(arg, "--frames") == 0)
      frames = atoi(next());
    else if (strcmp(arg, "--repeats") == 0)
      repeats = atoi(next());
    else if (strcmp(arg, "--size") == 0) {
      const char *size = next();
      unsigned w = 0, h = 0;
      if (sscanf(size, "%ux%u", &w, &h) != 2 || w == 0 || h == 0) {
        fprintf(stderr, "bench: --size wants WxH, got '%s'\n", size);
        return 2;
      }
      width = static_cast<uint16_t>(w);
      height = static_cast<uint16_t>(h);
    } else {
      fprintf(stderr, "bench: unknown argument '%s'\n", arg);
      return 2;
    }
  }
  if (!bench && check_path == nullptr && write_path == nullptr)
    bench = true;

  std::map<std::string, uint64_t> golden;
  if (check_path != nullptr && !read_golden(check_path, golden)) {
    fprintf(stderr, "bench: cannot read %s\n", check_path);
    return 2;
  }

  std::vector<Result> results;
  const size_t total = EffectRegistry::count();
  for (size_t i = 0; i < total; i++) {
    char name[64];
    effect_name(*EffectRegistry::at(i), name, sizeof(name));
    if (only != nullptr && strcmp(name, only) != 0)
      continue;
    results.push_back(run_effect(i, width, height, frames, repeats));
  }
  if (results.empty()) {
    fprintf(stderr, "bench: nothing to run\n");
    return 2;
  }

  int failures = 0;
  if (check_path != nullptr) {
    int missing = 0;
    for (const Result &result : results) {
      auto found = golden.find(result.name);
      if (found == golden.end()) {
        printf("MISSING %s %016llx\n", result.name.c_str(), (unsigned long long) result.hash);
        missing++;
        continue;
      }
      if (found->second != result.hash) {
        printf("CHANGED %s golden=%016llx now=%016llx\n", result.name.c_str(),
               (unsigned long long) found->second, (unsigned long long) result.hash);
        failures++;
      }
    }
    printf("golden: %zu effects, %d changed, %d not in the file\n", results.size(), failures, missing);
    if (failures == 0 && missing == 0)
      printf("GOLDEN OK\n");
  }

  if (write_path != nullptr) {
    FILE *file = fopen(write_path, "w");
    if (file == nullptr) {
      fprintf(stderr, "bench: cannot write %s\n", write_path);
      return 2;
    }
    fprintf(file,
            "# Canvas hashes for the wled_fx engine, written by tools/sim/bench.cpp.\n"
            "# One FNV-1a over every pixel of every frame: %d frames at %ux%u, %u ms a frame,\n"
            "# host RNG seeded with %08x. Regenerate with:\n"
            "#   powershell -File tools\\wsl\\wfx.ps1 bench --write tools/sim/golden.txt\n"
            "# A line that changes means the pixels changed. That is a bug unless the\n"
            "# commit is deliberately correcting the effect, in which case say so.\n",
            frames, width, height, STEP_MS, SEED);
    for (const Result &result : results)
      fprintf(file, "%s\t%016llx\n", result.name.c_str(), (unsigned long long) result.hash);
    fclose(file);
    printf("wrote %zu hashes to %s\n", results.size(), write_path);
  }

  if (bench) {
    std::vector<Result> sorted = results;
    std::sort(sorted.begin(), sorted.end(),
              [](const Result &a, const Result &b) { return a.us_per_frame > b.us_per_frame; });
    double total_us = 0.0;
    for (const Result &result : results)
      total_us += result.us_per_frame;
    printf("\n# %zu effects, %d frames each at %ux%u\n", results.size(), frames, width, height);
    printf("# us per frame, slowest first\n");
    for (const Result &result : sorted)
      printf("%10.1f  %s\n", result.us_per_frame, result.name.c_str());
    printf("\nTOTAL %.1f us  MEAN %.1f us  N %zu\n", total_us, total_us / results.size(), results.size());
  }

  return failures == 0 ? 0 : 1;
}
