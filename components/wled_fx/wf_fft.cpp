/* See wf_fft.h. The esp-dsp call sequence (init tables, transform, bit reverse)
 * follows WLED 16.0.1 usermods/audioreactive/audio_reactive.cpp.
 *
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * The generic backend below is original work for this repository.
 */

// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_fft.h"

#include <cmath>

#ifdef WLED_FX_USE_ESP_DSP
#include "dsps_fft2r.h"
#endif

namespace esphome {
namespace wled_fx {

#ifdef WLED_FX_USE_ESP_DSP

bool fft_init(uint16_t n) { return dsps_fft2r_init_fc32(nullptr, n) == ESP_OK; }

void fft_forward(float *data, uint16_t n) {
  // dsps_fft2r_fc32 resolves to the assembly version on targets that have one
  // (ae32 on the original ESP32, aes3 on the S3) and to the ANSI C version on
  // the rest. Both leave the result shuffled, hence the bit reversal.
  dsps_fft2r_fc32(data, n);
  dsps_bit_rev_fc32(data, n);
}

const char *fft_backend_name() { return "esp-dsp"; }

#else

namespace {

// exp(-2i * pi * k / n) for k in [0, n / 2). Filled once by fft_init().
float g_twiddle_re[FFT_MAX_SIZE / 2];
float g_twiddle_im[FFT_MAX_SIZE / 2];
uint16_t g_size = 0;

}  // namespace

bool fft_init(uint16_t n) {
  if (n < 2 || n > FFT_MAX_SIZE || (n & (n - 1)) != 0)
    return false;
  const double step = -2.0 * 3.14159265358979323846 / static_cast<double>(n);
  for (uint16_t k = 0; k < n / 2; k++) {
    g_twiddle_re[k] = static_cast<float>(std::cos(step * k));
    g_twiddle_im[k] = static_cast<float>(std::sin(step * k));
  }
  g_size = n;
  return true;
}

void fft_forward(float *data, uint16_t n) {
  if (n != g_size)
    return;

  // Decimation in time needs the input in bit reversed order.
  for (uint16_t i = 1, j = 0; i < n; i++) {
    uint16_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      const float tr = data[2 * i];
      const float ti = data[2 * i + 1];
      data[2 * i] = data[2 * j];
      data[2 * i + 1] = data[2 * j + 1];
      data[2 * j] = tr;
      data[2 * j + 1] = ti;
    }
  }

  for (uint16_t len = 2; len <= n; len <<= 1) {
    const uint16_t half = len >> 1;
    const uint16_t stride = n / len;
    for (uint16_t base = 0; base < n; base += len) {
      for (uint16_t k = 0; k < half; k++) {
        const float wr = g_twiddle_re[k * stride];
        const float wi = g_twiddle_im[k * stride];
        const uint16_t a = base + k;
        const uint16_t b = a + half;
        const float br = data[2 * b];
        const float bi = data[2 * b + 1];
        const float tr = br * wr - bi * wi;
        const float ti = br * wi + bi * wr;
        data[2 * b] = data[2 * a] - tr;
        data[2 * b + 1] = data[2 * a + 1] - ti;
        data[2 * a] += tr;
        data[2 * a + 1] += ti;
      }
    }
  }
}

const char *fft_backend_name() { return "generic"; }

#endif  // WLED_FX_USE_ESP_DSP

}  // namespace wled_fx
}  // namespace esphome
