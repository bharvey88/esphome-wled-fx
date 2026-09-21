/* See wf_audio_source.h. */

// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_audio_source.h"

#ifdef WLED_FX_HAS_AUDIO_SOURCE

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_timer.h>

#include <cstring>

#include "wf_fft.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.audio";

// Room for about a quarter of a second of audio, so a late FFT task loses
// nothing. WLED gets the same effect from the I2S DMA descriptors.
static const uint32_t RING_BUFFER_DURATION_MS = 250;
// Long enough to cover a whole block plus slack, short enough that a stopped
// microphone is noticed.
static const uint32_t READ_TIMEOUT_MS = 200;

static const uint32_t FFT_TASK_STACK_SIZE = 3072;
// Above the main loop (1) so a block is never delayed by rendering, well below
// the I2S reader (23) so sampling always wins.
static const UBaseType_t FFT_TASK_PRIORITY = 2;

/* WLED clears samplePeak once a frame has passed, from its main loop. The
 * analysis runs every 23 ms and so does the default frame clock, so without
 * this the flag could still be visible for two or three frames. */
static const uint32_t PEAK_DELAY_MS = 50;

// About three seconds of audio, by which point the smoothed cost has settled.
static const uint32_t COST_REPORT_AFTER_BLOCKS = 128;

namespace {

/* Copies the analysis results only. max_vol and bin_num belong to whichever
 * effect is running: it writes them through Segment::audio() and the peak
 * detector reads them back on the next block, exactly as WLED's u_data[6] and
 * u_data[7] work. Copying them here would throw the effect's value away. */
void copy_analysis(const AudioData &src, AudioData &dst) {
  dst.volume_smth = src.volume_smth;
  dst.volume_raw = src.volume_raw;
  std::memcpy(dst.fft_result, src.fft_result, sizeof(dst.fft_result));
  dst.sample_peak = src.sample_peak;
  dst.fft_major_peak = src.fft_major_peak;
  dst.my_magnitude = src.my_magnitude;
  dst.fft_major_peak_smth = src.fft_major_peak_smth;
  dst.sound_pressure = src.sound_pressure;
  dst.agc_sensitivity = src.agc_sensitivity;
  dst.zero_crossing_count = src.zero_crossing_count;
}

}  // namespace

void WledFxAudioSource::setup() {
  if (this->microphone_source_ == nullptr) {
    this->mark_failed();
    return;
  }

  const auto stream_info = this->microphone_source_->get_audio_stream_info();
  this->sample_rate_ = stream_info.get_sample_rate();
  if (stream_info.get_channels() != 1) {
    ESP_LOGE(TAG, "The analysis needs exactly one channel");
    this->mark_failed();
    return;
  }

  this->config_.sample_rate = this->sample_rate_;
  this->processor_.set_config(this->config_);
  if (!this->processor_.begin()) {
    ESP_LOGE(TAG, "FFT backend refused %u points", AudioProcessor::SAMPLES_FFT);
    this->mark_failed();
    return;
  }

  const size_t bytes_per_frame = stream_info.frames_to_bytes(1);
  const size_t ring_buffer_size = (stream_info.ms_to_bytes(RING_BUFFER_DURATION_MS) / bytes_per_frame) *
                                  bytes_per_frame;
  this->ring_buffer_ = ring_buffer::RingBuffer::create(ring_buffer_size);
  if (this->ring_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Ring buffer allocation failed");
    this->mark_failed();
    return;
  }

  /* The callback runs on the microphone's own task, so it only ever copies into
   * the ring buffer. write() drops the oldest bytes on overflow, which is the
   * right thing for an analysis that only cares about recent audio. */
  ring_buffer::RingBuffer *ring_buffer = this->ring_buffer_.get();
  this->microphone_source_->add_data_callback([this, ring_buffer](const std::vector<uint8_t> &data) {
    const size_t written = ring_buffer->write((void *) data.data(), data.size());
    if (written < data.size())
      this->overflow_count_++;
  });

  if (!this->task_.create(WledFxAudioSource::fft_task_entry, "wled_fx_fft", FFT_TASK_STACK_SIZE, (void *) this,
                          FFT_TASK_PRIORITY, this->task_in_psram_)) {
    ESP_LOGE(TAG, "FFT task could not be created");
    this->mark_failed();
    return;
  }

  // From here on the effects read real audio instead of the simulation.
  set_audio_source(this);

  if (!this->microphone_source_->is_passive())
    this->microphone_source_->start();
  this->running_ = true;
}

void WledFxAudioSource::start() {
  if (this->microphone_source_ == nullptr || this->microphone_source_->is_passive())
    return;
  this->microphone_source_->start();
}

void WledFxAudioSource::stop() {
  if (this->microphone_source_ == nullptr || this->microphone_source_->is_passive())
    return;
  this->microphone_source_->stop();
}

void WledFxAudioSource::loop() {
  bool copied = false;
  uint32_t peak_time;

  portENTER_CRITICAL(&this->spinlock_);
  if (this->have_shared_) {
    copy_analysis(this->shared_, this->front_);
    this->have_shared_ = false;
    copied = true;
  }
  peak_time = this->peak_time_;
  portEXIT_CRITICAL(&this->spinlock_);

  if (copied)
    this->have_data_ = true;

  if (this->have_data_ && (millis() - peak_time > PEAK_DELAY_MS))
    this->front_.sample_peak = 0;

  /* dump_config() runs before a single block has been analysed, so the cost is
   * reported here instead, once, after a few seconds of real audio. */
  if (!this->cost_logged_ && this->block_count_ >= COST_REPORT_AFTER_BLOCKS) {
    this->cost_logged_ = true;
    const float block_us = 1e6f * AudioProcessor::SAMPLES_FFT / static_cast<float>(this->sample_rate_);
    ESP_LOGD(TAG, "Analysis costs %" PRIu32 " us per %u sample block, %.1f%% of one core",
             this->fft_duration_us_, AudioProcessor::SAMPLES_FFT,
             100.0f * static_cast<float>(this->fft_duration_us_) / block_us);
  }
}

bool WledFxAudioSource::collect_block_() {
  constexpr size_t BLOCK_BYTES = AudioProcessor::SAMPLES_FFT * sizeof(int16_t);
  uint8_t *const dest = reinterpret_cast<uint8_t *>(this->block_);
  size_t filled = 0;

  while (filled < BLOCK_BYTES) {
    const size_t read =
        this->ring_buffer_->read(dest + filled, BLOCK_BYTES - filled, pdMS_TO_TICKS(READ_TIMEOUT_MS));
    if (read == 0)
      return false;  // the microphone stopped or stalled; the caller retries
    filled += read;
  }
  return true;
}

void WledFxAudioSource::fft_task_entry(void *parameter) {
  static_cast<WledFxAudioSource *>(parameter)->fft_task_();
}

void WledFxAudioSource::fft_task_() {
  for (;;) {
    if (!this->collect_block_())
      continue;

    // Pick up whatever the running effect asked the peak detector to watch.
    uint8_t max_vol;
    uint8_t bin_num;
    portENTER_CRITICAL(&this->spinlock_);
    max_vol = this->front_.max_vol;
    bin_num = this->front_.bin_num;
    portEXIT_CRITICAL(&this->spinlock_);
    this->processor_.set_peak_controls(max_vol, bin_num);

    const int64_t started = esp_timer_get_time();
    this->processor_.process_block(this->block_, millis());
    const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);

    portENTER_CRITICAL(&this->spinlock_);
    copy_analysis(this->processor_.data(), this->shared_);
    this->peak_time_ = this->processor_.peak_time();
    this->have_shared_ = true;
    portEXIT_CRITICAL(&this->spinlock_);

    // Smoothed the way WLED smooths its own fftTime.
    this->fft_duration_us_ = (elapsed * 3 + this->fft_duration_us_ * 7) / 10;
    this->block_count_++;
  }
}

void WledFxAudioSource::dump_config() {
  static const char *const AGC_NAMES[] = {"off", "normal", "vivid", "lazy"};
  static const char *const SCALING_NAMES[] = {"none", "logarithmic", "linear", "square root"};

  ESP_LOGCONFIG(TAG,
                "wled_fx audio:\n"
                "  Sample rate: %" PRIu32 " Hz (%u point FFT, %.1f Hz per bin)\n"
                "  FFT backend: %s\n"
                "  Gain: %u, squelch: %u, input level: %u\n"
                "  AGC: %s, scaling: %s\n"
                "  Limiter: %s (attack %u ms, decay %u ms)\n"
                "  Microphone filter: %s, band pass mapping: %s",
                this->sample_rate_, AudioProcessor::SAMPLES_FFT, this->processor_.bin_width(), fft_backend_name(),
                this->config_.gain, this->config_.squelch, this->config_.input_level, AGC_NAMES[this->config_.agc],
                SCALING_NAMES[this->config_.scaling], YESNO(this->config_.limiter), this->config_.attack_ms,
                this->config_.decay_ms, YESNO(this->config_.mic_filter), YESNO(this->config_.bandpass));
}

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_HAS_AUDIO_SOURCE
