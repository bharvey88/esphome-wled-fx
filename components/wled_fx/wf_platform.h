#pragma once

// Platform shim so the engine builds both inside ESPHome and in the host simulator
// (tools/sim). Nothing in engine/, math/ or the effect files may include an ESPHome
// header directly; everything they need from the platform comes from here.

#include <cstddef>
#include <cstdint>

/* Whether this build has PSRAM, which is the one platform fact the engine needs
 * at compile time: WLED sizes its effect scratch budget from it (see
 * MAX_NUM_SEGMENTS in wf_segment.h).
 *
 * ESPHome's `psram:` component adds `USE_PSRAM` to the generated defines, and
 * `BOARD_HAS_PSRAM` as well on the Arduino framework; `CONFIG_SPIRAM` is esp-idf's
 * own spelling and is checked in case the flag arrives from sdkconfig instead.
 * The two host builds, the simulator and the ESPHome host platform, stand in for
 * the reference board, an ESP32-S3 with PSRAM, so they say yes. */
#ifdef WLED_FX_HOST_BUILD
#define WLED_FX_PSRAM 1
#define WLED_FX_ESP32S2 0
#define WLED_FX_ESP8266 0
#else
#include "esphome/core/defines.h"
/* `USE_PSRAM` means the configuration has a `psram:` block, where upstream's
 * `BOARD_HAS_PSRAM` means the board definition says the chip has it. They are
 * not the same question: an ESP32-S3 with PSRAM whose YAML omits `psram:`
 * compiles to the no-PSRAM profile here and renders twice the particles a WLED
 * device does. There is nothing better to key on at compile time, because
 * without the block the PSRAM support is not compiled in at all, so
 * dump_config prints which profile the build took. */
#if defined(USE_PSRAM) || defined(USE_HOST) || defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
#define WLED_FX_PSRAM 1
#else
#define WLED_FX_PSRAM 0
#endif
/* ESPHome's own variant spelling. `CONFIG_IDF_TARGET_ESP32S2` is not visible
 * here: it is an sdkconfig.h symbol and nothing in this component's include
 * chain reaches that file. */
#if defined(USE_ESP32_VARIANT_ESP32S2)
#define WLED_FX_ESP32S2 1
#else
#define WLED_FX_ESP32S2 0
#endif
#if defined(USE_ESP8266)
#define WLED_FX_ESP8266 1
#else
#define WLED_FX_ESP8266 0
#endif
#endif

namespace esphome {
namespace wled_fx {

// Uniformly distributed 32 bit value. On device this is the hardware RNG behind
// esphome::random_uint32(); on the host it is a deterministic xorshift so simulator
// runs are reproducible.
uint32_t platform_random_u32();

// Seeds the host simulator RNG. No effect on device.
void platform_seed_random(uint32_t seed);

// Milliseconds since boot.
uint32_t platform_millis();

// One-shot buffer allocation, used for the canvas and for effect scratch data.
// Returns nullptr on failure. Never called per frame.
void *platform_alloc(size_t size);
void platform_free(void *ptr);

/* --- where the hot buffers live ---------------------------------------------
 *
 * platform_alloc() is PSRAM first, because that is what ESPHome's default
 * RAMAllocator does and because the effect scratch block reaches 25 KB on the
 * particle effects, which is more internal RAM than an ESP32-S3 running wifi,
 * the API and a HUB75 driver has to give.
 *
 * The canvas is the other case. It is 16 KB on a 64x64 panel and every effect
 * reads and writes it several times a frame, in scattered order: blur reads
 * five neighbours per pixel, fade and the particle renderers read, modify and
 * write, and none of that is the long sequential burst PSRAM is good at. On an
 * ESP32-S3 an internal SRAM word is a load; a PSRAM word that misses the cache
 * is a load plus an external transaction. Putting the canvas in internal RAM
 * when there is room for it is the largest thing this component can do about
 * the per-pixel cost without changing a single effect.
 *
 * "When there is room for it" is the whole difficulty: the wifi stack, the API
 * server and the logger all take internal RAM for the life of the device, and
 * several of those allocations happen after setup(). AUTO therefore allocates,
 * looks at what is left, and hands the block straight back if it took too
 * much. The floors are in wf_platform.cpp. */
enum class MemoryPolicy : uint8_t {
  AUTO = 0,  // internal RAM while enough is left over, PSRAM otherwise
  INTERNAL,  // internal RAM whenever the allocation succeeds at all
  PSRAM,     // never internal; whatever platform_alloc() would do
};

void platform_set_memory_policy(MemoryPolicy policy);
MemoryPolicy platform_memory_policy();
// "auto", "internal" or "psram", for dump_config.
const char *platform_memory_policy_name();

/* Like platform_alloc(), for a small buffer the CPU touches every frame.
 * `internal_out`, when given, says where the block actually landed, so the
 * front end can print it rather than leave the user guessing. */
void *platform_alloc_fast(size_t size, bool *internal_out = nullptr);

// Internal heap figures, for the line dump_config prints. Both are zero on a
// build with no separate internal region.
size_t platform_internal_free();
size_t platform_internal_largest_block();

}  // namespace wled_fx
}  // namespace esphome
