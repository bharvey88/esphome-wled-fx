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

/* The host has one kind of memory, so the policy is recorded and ignored. It is
 * still recorded rather than dropped, because the simulator prints it and a
 * reader comparing a simulator run with a device log should see the same word. */
namespace {
MemoryPolicy host_policy = MemoryPolicy::AUTO;
}  // namespace

void platform_set_memory_policy(MemoryPolicy policy) { host_policy = policy; }
MemoryPolicy platform_memory_policy() { return host_policy; }

void *platform_alloc_fast(size_t size, bool *internal_out) {
  if (internal_out != nullptr)
    *internal_out = true;
  return platform_alloc(size);
}

size_t platform_internal_free() { return 0; }
size_t platform_internal_largest_block() { return 0; }

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

namespace {

MemoryPolicy memory_policy = MemoryPolicy::AUTO;

/* What AUTO insists on leaving behind in internal RAM.
 *
 * The figures come from a profile run of examples/hardware-test/m1-test.yaml on
 * an Apollo M-1: with wifi up, the API connected and the log streaming, that
 * build settles at about 72 KB of internal heap free with a largest free block
 * of 31 KB, and the HUB75 driver has already taken roughly 160 KB of internal
 * SRAM for its two bit-plane framebuffers and its descriptor chains. setup()
 * runs before the API has a client, so it always sees more free memory than the
 * device will have a minute later, which is why the floors are well above zero.
 *
 * Both have to hold. A large total made of nothing but small holes is not
 * memory anything can use, and it is a single large block that the next TLS
 * handshake or OTA buffer will want. */
constexpr size_t INTERNAL_FREE_FLOOR = 48 * 1024;
constexpr size_t INTERNAL_BLOCK_FLOOR = 16 * 1024;

RAMAllocator<uint8_t> internal_allocator() {
  return RAMAllocator<uint8_t>(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
}

bool internal_headroom_left() {
  const RAMAllocator<uint8_t> allocator = internal_allocator();
  return allocator.get_free_heap_size() >= INTERNAL_FREE_FLOOR &&
         allocator.get_max_free_block_size() >= INTERNAL_BLOCK_FLOOR;
}

}  // namespace

void platform_set_memory_policy(MemoryPolicy policy) { memory_policy = policy; }
MemoryPolicy platform_memory_policy() { return memory_policy; }

void *platform_alloc_fast(size_t size, bool *internal_out) {
  if (internal_out != nullptr)
    *internal_out = false;
  if (size == 0)
    return nullptr;
  if (memory_policy != MemoryPolicy::PSRAM) {
    RAMAllocator<uint8_t> allocator = internal_allocator();
    uint8_t *buffer = allocator.allocate(size);
    if (buffer != nullptr) {
      /* Measured, not predicted. Asking the heap what it has left after the
       * block is gone is exact, where working it out from the largest free
       * block beforehand assumes which region the allocator chose. */
      if (memory_policy == MemoryPolicy::INTERNAL || internal_headroom_left()) {
        memset(buffer, 0, size);
        if (internal_out != nullptr)
          *internal_out = true;
        return buffer;
      }
      allocator.deallocate(buffer, size);
    }
  }
  /* Either the policy said not to, there was no internal block that size, or
   * taking it would have left too little behind. PSRAM is slower and it works,
   * which is the right way round: a panel that renders at 30 fps is a
   * disappointment and a device that cannot join wifi is a brick on a wall. */
  return platform_alloc(size);
}

size_t platform_internal_free() { return internal_allocator().get_free_heap_size(); }
size_t platform_internal_largest_block() { return internal_allocator().get_max_free_block_size(); }

}  // namespace wled_fx
}  // namespace esphome

#endif

namespace esphome {
namespace wled_fx {

// Same spelling as the YAML option, so the log line and the configuration read
// the same. Shared by both builds.
const char *platform_memory_policy_name() {
  switch (platform_memory_policy()) {
    case MemoryPolicy::INTERNAL:
      return "internal";
    case MemoryPolicy::PSRAM:
      return "psram";
    default:
      return "auto";
  }
}

}  // namespace wled_fx
}  // namespace esphome
