#pragma once

/* The FFT backend used by the audio processing core.
 *
 * Two implementations live behind this interface, picked at compile time:
 *
 *   espressif/esp-dsp  on ESP32 when the audio source is configured, which is
 *                      what pulls the managed component into the build. Same
 *                      call sequence WLED's audioreactive usermod uses.
 *   a self-contained   everywhere else, including the host simulator. Plain
 *   radix-2 FFT        C++, no dependencies, about 90 lines.
 *
 * Nothing above this header knows which one is in use, so the processing core
 * stays framework free and the simulator exercises the same pipeline.
 */

#include <cstdint>

namespace esphome {
namespace wled_fx {

// Largest transform this module supports. The pipeline only ever asks for 512.
inline constexpr uint16_t FFT_MAX_SIZE = 512;

/* Prepares the backend for n point complex transforms. n must be a power of two
 * and at most FFT_MAX_SIZE. Call once, at setup: the esp-dsp backend allocates
 * its twiddle tables here and the generic one fills a static table. Returns
 * false if the backend refused, in which case fft_forward() must not be called. */
bool fft_init(uint16_t n);

/* In-place forward complex FFT over n interleaved [Re, Im] pairs, so `data` is
 * 2 * n floats. Output is in natural bin order. No allocation, no locking. */
void fft_forward(float *data, uint16_t n);

// "esp-dsp" or "generic", for dump_config() and the test output.
const char *fft_backend_name();

}  // namespace wled_fx
}  // namespace esphome
