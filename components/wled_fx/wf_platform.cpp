// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_platform.h"

#ifdef WLED_FX_HOST_BUILD

#include <chrono>
#include <cstdlib>

namespace esphome {
namespace wled_fx {

namespace {
uint32_t host_rng_state = 0x2545F491u;
}  // namespace

uint32_t platform_random_u32() {
  uint32_t x = host_rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  host_rng_state = x;
  return x;
}

void platform_seed_random(uint32_t seed) { host_rng_state = seed ? seed : 0x2545F491u; }

uint32_t platform_millis() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void *platform_alloc(size_t size) { return std::calloc(1, size); }

void platform_free(void *ptr) { std::free(ptr); }

}  // namespace wled_fx
}  // namespace esphome

#else  // ESPHome build

#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace wled_fx {

uint32_t platform_random_u32() { return random_uint32(); }

void platform_seed_random(uint32_t seed) { (void) seed; }

uint32_t platform_millis() { return millis(); }

void *platform_alloc(size_t size) {
  RAMAllocator<uint8_t> allocator;
  uint8_t *buffer = allocator.allocate(size);
  if (buffer != nullptr) {
    memset(buffer, 0, size);
  }
  return buffer;
}

void platform_free(void *ptr) {
  if (ptr == nullptr)
    return;
  RAMAllocator<uint8_t> allocator;
  allocator.deallocate(static_cast<uint8_t *>(ptr), 0);
}

}  // namespace wled_fx
}  // namespace esphome

#endif
