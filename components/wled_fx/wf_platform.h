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

}  // namespace wled_fx
}  // namespace esphome
