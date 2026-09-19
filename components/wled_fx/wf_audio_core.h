#pragma once

/* The audio analysis pipeline, ported from WLED 16.0.1
 * usermods/audioreactive/audio_reactive.cpp.
 *
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Sample filtering, AGC, GEQ mapping and post-processing are largely the work of
 * @softhack007 and @blazoncek in that usermod.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * This file is framework free on purpose: it has no ESPHome, FreeRTOS or Arduino
 * dependency, takes its samples as a plain int16 array and its clock as an
 * argument, so the host simulator runs the same code the firmware does. The
 * microphone, the task and the YAML live in wf_audio_source.*, the FFT itself in
 * wf_fft.*.
 */

#include <cstddef>
#include <cstdint>

#include "wf_audio.h"

namespace esphome {
namespace wled_fx {

// WLED's soundAgc: 0 off, then the three presets in agcSampleDecay[] order.
enum AgcPreset : uint8_t {
  AGC_OFF = 0,
  AGC_NORMAL = 1,
  AGC_VIVID = 2,
  AGC_LAZY = 3,
};

// WLED's FFTScalingMode.
enum FftScaling : uint8_t {
  FFT_SCALE_NONE = 0,
  FFT_SCALE_LOG = 1,
  FFT_SCALE_LINEAR = 2,
  FFT_SCALE_SQRT = 3,
};

/* The usermod's user settable options. Field names follow this component's
 * YAML keys; the WLED name each one carries is in the comment. */
struct AudioConfig {
  // Sampling rate of the incoming PCM. WLED hardcodes 22050; the bin mapping is
  // rescaled from that reference when a microphone runs at another rate.
  uint32_t sample_rate{22050};
  // WLED `sampleGain`, the manual input gain. Ignored while AGC is on.
  uint8_t gain{60};
  // WLED `soundSquelch`, the noise gate.
  uint8_t squelch{10};
  // WLED `inputLevel`. With AGC on it is the "GEQ gain" post-amplifier.
  uint8_t input_level{128};
  // WLED `soundAgc`.
  uint8_t agc{AGC_NORMAL};
  // WLED `FFTScalingMode`.
  uint8_t scaling{FFT_SCALE_SQRT};
  // WLED `limiterOn`, the dynamics limiter on volume_smth.
  bool limiter{true};
  // WLED `attackTime` / `decayTime`, in milliseconds.
  uint16_t attack_ms{80};
  uint16_t decay_ms{1400};
  // WLED `useMicFilter`, an IIR band pass over the raw samples, before the FFT.
  bool mic_filter{false};
  // WLED `useBandPassFilter`, the alternative low-end bin mapping, after the FFT.
  bool bandpass{false};
};

/* One instance owns every buffer the pipeline needs, all of them members, so
 * nothing is allocated once begin() has run. Not thread safe: call begin(),
 * process_block() and data() from one task, and copy the result out under a
 * lock if another task needs it. */
class AudioProcessor {
 public:
  // WLED `samplesFFT` and `samplesFFT_2`. Must stay a power of two.
  static constexpr uint16_t SAMPLES_FFT = 512;
  static constexpr uint16_t SAMPLES_FFT_2 = 256;

  /* Applies the options and recomputes the window and the bin mapping. Safe to
   * call before begin() or between blocks; it does not disturb the filters. */
  void set_config(const AudioConfig &config);
  const AudioConfig &config() const { return this->config_; }

  /* Initialises the FFT backend and clears all filter state. Returns false when
   * the backend refused, in which case process_block() does nothing. */
  bool begin();
  // Clears the filters, the smoothing history and the published results.
  void reset();

  /* Runs the whole pipeline over exactly SAMPLES_FFT mono samples: pre-filter,
   * volume filters and AGC, FFT, GEQ mapping, post-processing, peak detection.
   * `now_ms` is the current time in milliseconds, which is all the clock this
   * code gets. */
  void process_block(const int16_t *samples, uint32_t now_ms);

  const AudioData &data() const { return this->data_; }

  /* WLED lets an effect write maxVol and binNum back from its own sliders, and
   * the peak detector reads them on the next block. */
  void set_peak_controls(uint8_t max_vol, uint8_t bin_num);

  // The AGC multiplier in use, for logging. WLED `multAgc`.
  float agc_multiplier() const { return this->mult_agc_; }
  /* When the last sample peak was detected. WLED clears samplePeak from its main
   * loop as well as from the FFT task, so the flag lasts about one frame rather
   * than one analysis block; the front end needs this to do the same. */
  uint32_t peak_time() const { return this->time_of_peak_; }
  // Hz per FFT bin at the configured sample rate.
  float bin_width() const { return static_cast<float>(this->config_.sample_rate) / SAMPLES_FFT; }

 protected:
  void rebuild_bins_();
  void build_window_();
  void run_mic_filter_();
  void run_fft_(uint32_t now_ms);
  float fft_add_avg_(uint16_t from, uint16_t to) const;
  void map_frequency_channels_();
  void post_process_fft_results_(bool noise_gate_open);
  void get_sample_(uint32_t now_ms);
  void agc_avg_(uint32_t the_time, uint32_t now_ms);
  void limit_sample_dynamics_(uint32_t now_ms);
  void detect_sample_peak_(uint32_t now_ms);
  void auto_reset_peak_(uint32_t now_ms);

  AudioConfig config_{};
  AudioData data_{};

  /* WLED's valFFT: 512 real samples first, then the same buffer reused as 512
   * interleaved [Re, Im] pairs, then the lower 256 entries overwritten with the
   * magnitudes. 16 byte aligned because the esp-dsp assembly kernels need it. */
  alignas(16) float fft_[SAMPLES_FFT * 2]{};
  // WLED's windowFFT, a Blackman-Harris window (FFT_PREFER_EXACT_PEAKS).
  float window_[SAMPLES_FFT]{};

  // Bin ranges per GEQ channel, rescaled from WLED's 22050 Hz reference.
  uint16_t bin_from_[NUM_GEQ_CHANNELS]{};
  uint16_t bin_to_[NUM_GEQ_CHANNELS]{};
  float bin_damp_[NUM_GEQ_CHANNELS]{};

  // WLED fftCalc / fftAvg.
  float fft_calc_[NUM_GEQ_CHANNELS]{};
  float fft_avg_[NUM_GEQ_CHANNELS]{};

  // runMicFilter state.
  float filter_last_vals_[2]{};
  float filter_lowfilt_{0.0f};

  // getSample() and agcAvg() state, upstream names in comments.
  float mic_data_real_{0.0f};   // micDataReal
  double mic_lev_{0.0};         // micLev
  float exp_adj_f_{0.0f};       // expAdjF
  float sample_real_{0.0f};     // sampleReal
  int16_t sample_raw_{0};       // sampleRaw
  int16_t raw_sample_agc_{0};   // rawSampleAgc
  float sample_avg_{0.0f};      // sampleAvg
  float sample_agc_{0.0f};      // sampleAgc
  float mult_agc_{1.0f};        // multAgc
  double sample_max_{0.0};      // sampleMax
  double control_integrated_{0.0};
  int last_sound_agc_{-1};

  float fft_major_peak_{1.0f};  // FFT_MajorPeak
  float fft_magnitude_{0.0f};   // FFT_Magnitude

  uint32_t time_of_peak_{0};
  uint32_t last_agc_time_{0};
  uint32_t last_um_run_{0};
  uint32_t last_limiter_time_{0};
  float last_volume_smth_{0.0f};
  bool sample_peak_{false};
  bool ready_{false};
};

}  // namespace wled_fx
}  // namespace esphome
