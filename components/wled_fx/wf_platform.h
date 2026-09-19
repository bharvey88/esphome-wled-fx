#pragma once

// Platform shim so the engine builds both inside ESPHome and in the host simulator
// (tools/sim). Nothing in engine/, math/ or the effect files may include an ESPHome
// header directly; everything they need from the platform comes from here.

#include <cstddef>
#include <cstdint>

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
