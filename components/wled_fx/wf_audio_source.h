#pragma once

/* The ESPHome side of the audio source: a microphone, a ring buffer, a task and
 * the hand-off to the main loop. All of the analysis is in wf_audio_core.*, which
 * knows nothing about ESPHome; this file is only plumbing.
 *
 * The structure follows ESPHome's `sound_level` component (microphone source,
 * ring buffer written from the microphone callback) and `micro_wake_word` (a
 * StaticTask doing the heavy maths off the main loop).
 *
 * The whole file compiles to nothing unless the YAML configured an `audio:`
 * block, which is what keeps the microphone and esp-dsp dependencies out of
 * builds that do not ask for them, and lets the host simulator compile the
 * directory unchanged.
 */

#if defined(WLED_FX_AUDIO) && !defined(WLED_FX_HOST_BUILD)
#define WLED_FX_HAS_AUDIO_SOURCE
#endif

#ifdef WLED_FX_HAS_AUDIO_SOURCE

#include "esphome/components/microphone/microphone_source.h"
#include "esphome/components/ring_buffer/ring_buffer.h"
#include "esphome/core/component.h"
#include "esphome/core/static_task.h"

#include <freertos/FreeRTOS.h>

#include <memory>

#include "wf_audio.h"
#include "wf_audio_core.h"

namespace esphome {
namespace wled_fx {

class WledFxAudioSource : public Component, public AudioSource {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_microphone_source(microphone::MicrophoneSource *microphone_source) {
    this->microphone_source_ = microphone_source;
  }

  // Codegen calls these before setup(), one per YAML key.
  void set_gain(uint8_t gain) { this->config_.gain = gain; }
  void set_squelch(uint8_t squelch) { this->config_.squelch = squelch; }
  void set_input_level(uint8_t input_level) { this->config_.input_level = input_level; }
  void set_agc(uint8_t agc) { this->config_.agc = agc; }
  void set_scaling(uint8_t scaling) { this->config_.scaling = scaling; }
  void set_limiter(bool limiter) { this->config_.limiter = limiter; }
  void set_attack(uint16_t attack_ms) { this->config_.attack_ms = attack_ms; }
  void set_decay(uint16_t decay_ms) { this->config_.decay_ms = decay_ms; }
  void set_mic_filter(bool mic_filter) { this->config_.mic_filter = mic_filter; }
  void set_bandpass(bool bandpass) { this->config_.bandpass = bandpass; }
  void set_task_in_psram(bool task_in_psram) { this->task_in_psram_ = task_in_psram; }

  // AudioSource, read by the effects through Segment::audio().
  const AudioData &data() const override { return this->front_; }
  bool has_data() const override { return this->have_data_; }

  void start();
  void stop();

 protected:
  static void fft_task_entry(void *parameter);
  void fft_task_();
  bool collect_block_();

  microphone::MicrophoneSource *microphone_source_{nullptr};
  std::shared_ptr<ring_buffer::RingBuffer> ring_buffer_;

  AudioProcessor processor_;
  AudioConfig config_{};

  /* Three copies of the struct, which is 64 bytes, so this is cheap:
   *   processor_.data()  owned by the FFT task
   *   shared_            handed over under the spinlock
   *   front_             owned by the main loop, and the one effects read and
   *                      write max_vol / bin_num into
   * Only the analysis fields are copied between them, so an effect's max_vol and
   * bin_num survive every hand-off and reach the peak detector on the next block. */
  AudioData shared_{};
  AudioData front_{};
  // Braces rather than an assignment would try to initialise the first member
  // from the whole list, because the macro is itself a braced aggregate.
  portMUX_TYPE spinlock_ = portMUX_INITIALIZER_UNLOCKED;

  int16_t block_[AudioProcessor::SAMPLES_FFT]{};
  StaticTask task_;

  uint32_t peak_time_{0};
  uint32_t fft_duration_us_{0};
  uint32_t block_count_{0};
  uint32_t overflow_count_{0};
  uint32_t sample_rate_{0};
  bool task_in_psram_{false};
  bool have_shared_{false};
  bool have_data_{false};
  bool running_{false};
  bool cost_logged_{false};
};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_HAS_AUDIO_SOURCE
