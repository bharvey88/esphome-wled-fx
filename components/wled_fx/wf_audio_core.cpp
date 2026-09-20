/* See wf_audio_core.h for the WLED attribution and licence notice.
 *
 * The pipeline below follows WLED 16.0.1 usermods/audioreactive/audio_reactive.cpp
 * step by step: FFTcode(), runMicFilter(), postProcessFFTResults(),
 * detectSamplePeak(), autoResetPeak(), and the usermod's getSample(), agcAvg()
 * and limitSampleDynamics(). Upstream variable names are kept in comments so the
 * two can be diffed. The deviations are listed in PORTING.md.
 */

#include "wf_audio_core.h"

#include <cmath>
#include <cstring>

#include "wf_fft.h"
#include "wf_math.h"

namespace esphome {
namespace wled_fx {

namespace {

// WLED's SAMPLE_RATE. Every bin index in the GEQ table below is an index into a
// 512 point FFT taken at this rate, which is what makes rescaling possible.
constexpr float REFERENCE_SAMPLE_RATE = 22050.0f;

// WLED FFT_DOWNSCALE for the Blackman-Harris window (FFT_PREFER_EXACT_PEAKS).
constexpr float FFT_DOWNSCALE = 0.40f;
// WLED LOG_256.
constexpr float LOG_256 = 5.54517744f;

// Table of multiplication factors so that we can even out the frequency
// response. WLED fftResultPink.
constexpr float FFT_RESULT_PINK[NUM_GEQ_CHANNELS] = {1.70f, 1.71f, 1.73f, 1.78f, 1.68f, 1.56f,
                                                     1.55f, 1.63f, 1.79f, 1.62f, 1.80f, 2.06f,
                                                     2.47f, 3.35f, 6.83f, 9.55f};

// AGC presets: normal, vivid, lazy.
constexpr double AGC_SAMPLE_DECAY[3] = {0.9994, 0.9985, 0.9997};
constexpr float AGC_ZONE_LOW[3] = {32, 28, 36};
constexpr float AGC_ZONE_HIGH[3] = {240, 240, 248};
constexpr float AGC_ZONE_STOP[3] = {336, 448, 304};
constexpr float AGC_TARGET0[3] = {112, 144, 164};
constexpr float AGC_TARGET0_UP[3] = {88, 64, 116};
constexpr float AGC_TARGET1[3] = {220, 224, 216};
constexpr double AGC_FOLLOW_FAST[3] = {1 / 192.0, 1 / 128.0, 1 / 256.0};
constexpr double AGC_FOLLOW_SLOW[3] = {1 / 6144.0, 1 / 4096.0, 1 / 8192.0};
constexpr double AGC_CONTROL_KP[3] = {0.6, 1.5, 0.65};
constexpr double AGC_CONTROL_KI[3] = {1.7, 1.85, 1.2};
constexpr float AGC_SAMPLE_SMOOTH[3] = {1 / 12.0f, 1 / 6.0f, 1 / 16.0f};

/* The GEQ channel to FFT bin mapping, "optimized for 22050 Hz by softhack007".
 * The comments are upstream's, and are only true at 22050 Hz; at another sample
 * rate the same frequencies land on different bins, which is what
 * rebuild_bins_() corrects for. */
struct BinRange {
  uint16_t from;
  uint16_t to;
  float damp;
};

constexpr BinRange BINS_NORMAL[NUM_GEQ_CHANNELS] = {
    {1, 2, 1.0f},      // 43 - 86    sub-bass
    {2, 3, 1.0f},      // 86 - 129   bass
    {3, 5, 1.0f},      // 129 - 216  bass
    {5, 7, 1.0f},      // 216 - 301  bass + midrange
    {7, 10, 1.0f},     // 301 - 430  midrange
    {10, 13, 1.0f},    // 430 - 560  midrange
    {13, 19, 1.0f},    // 560 - 818  midrange
    {19, 26, 1.0f},    // 818 - 1120 midrange, 1 kHz is always the centre
    {26, 33, 1.0f},    // 1120 - 1421 midrange
    {33, 44, 1.0f},    // 1421 - 1895 midrange
    {44, 56, 1.0f},    // 1895 - 2412 midrange + high mid
    {56, 70, 1.0f},    // 2412 - 3015 high mid
    {70, 86, 1.0f},    // 3015 - 3704 high mid
    {86, 104, 1.0f},   // 3704 - 4479 high mid
    {104, 165, 0.88f}, // 4479 - 7106 high mid + high, slight damping
    {165, 215, 0.70f}, // 7106 - 9259 high, some damping
};

// The band pass variant only differs at the bottom and at the very top.
constexpr BinRange BINS_BANDPASS[NUM_GEQ_CHANNELS] = {
    {3, 4, 0.8f},   {4, 5, 0.9f},   {5, 6, 1.0f},     {6, 7, 1.0f},
    {7, 10, 1.0f},  {10, 13, 1.0f}, {13, 19, 1.0f},   {19, 26, 1.0f},
    {26, 33, 1.0f}, {33, 44, 1.0f}, {44, 56, 1.0f},   {56, 70, 1.0f},
    {70, 86, 1.0f}, {86, 104, 1.0f}, {104, 165, 0.88f}, {165, 205, 0.75f},
};

}  // namespace

void AudioProcessor::set_config(const AudioConfig &config) {
  this->config_ = config;
  if (this->config_.sample_rate == 0)
    this->config_.sample_rate = static_cast<uint32_t>(REFERENCE_SAMPLE_RATE);
  if (this->config_.agc > AGC_LAZY)
    this->config_.agc = AGC_LAZY;
  if (this->config_.scaling > FFT_SCALE_SQRT)
    this->config_.scaling = FFT_SCALE_SQRT;
  this->rebuild_bins_();
  this->build_window_();
}

void AudioProcessor::rebuild_bins_() {
  /* WLED only ever samples at 22050 Hz, so it can hardcode bin indices. A
   * microphone here may run at 16000 or 44100, which moves every frequency to a
   * different bin. Scaling the reference indices by the rate ratio keeps each
   * GEQ channel over the frequency band upstream intended. */
  const float scale = REFERENCE_SAMPLE_RATE / static_cast<float>(this->config_.sample_rate);
  const BinRange *table = this->config_.bandpass ? BINS_BANDPASS : BINS_NORMAL;
  const uint16_t highest = SAMPLES_FFT_2 - 1;

  for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++) {
    long from = lroundf(static_cast<float>(table[i].from) * scale);
    long to = lroundf(static_cast<float>(table[i].to) * scale);
    if (from < 1)
      from = 1;
    if (to < from)
      to = from;
    if (from > highest)
      from = highest;
    if (to > highest)
      to = highest;
    this->bin_from_[i] = static_cast<uint16_t>(from);
    this->bin_to_[i] = static_cast<uint16_t>(to);
    this->bin_damp_[i] = table[i].damp;
  }
}

void AudioProcessor::build_window_() {
  // Blackman-Harris, the same 4 term window dsps_wind_blackman_harris_f32 and
  // arduinoFFT build, so both FFT backends see identical input.
  constexpr float A0 = 0.35875f;
  constexpr float A1 = 0.48829f;
  constexpr float A2 = 0.14128f;
  constexpr float A3 = 0.01168f;
  const double step = 2.0 * 3.14159265358979323846 / static_cast<double>(SAMPLES_FFT - 1);
  for (uint16_t i = 0; i < SAMPLES_FFT; i++) {
    const double x = step * i;
    this->window_[i] = static_cast<float>(A0 - A1 * std::cos(x) + A2 * std::cos(2.0 * x) - A3 * std::cos(3.0 * x));
  }
}

bool AudioProcessor::begin() {
  this->build_window_();
  this->rebuild_bins_();
  this->reset();
  this->ready_ = fft_init(SAMPLES_FFT);
  return this->ready_;
}

void AudioProcessor::reset() {
  std::memset(this->fft_, 0, sizeof(this->fft_));
  std::memset(this->fft_calc_, 0, sizeof(this->fft_calc_));
  std::memset(this->fft_avg_, 0, sizeof(this->fft_avg_));
  this->filter_last_vals_[0] = this->filter_last_vals_[1] = 0.0f;
  this->filter_lowfilt_ = 0.0f;
  this->mic_data_real_ = 0.0f;
  this->mic_lev_ = 0.0;
  this->exp_adj_f_ = 0.0f;
  this->sample_real_ = 0.0f;
  this->sample_raw_ = 0;
  this->raw_sample_agc_ = 0;
  this->sample_avg_ = 0.0f;
  this->sample_agc_ = 0.0f;
  this->mult_agc_ = 1.0f;
  this->sample_max_ = 0.0;
  this->control_integrated_ = 0.0;
  this->last_sound_agc_ = -1;
  this->fft_major_peak_ = 1.0f;
  this->fft_magnitude_ = 0.0f;
  this->time_of_peak_ = 0;
  this->last_agc_time_ = 0;
  this->last_um_run_ = 0;
  this->last_limiter_time_ = 0;
  this->last_volume_smth_ = 0.0f;
  this->sample_peak_ = false;

  const uint8_t max_vol = this->data_.max_vol;
  const uint8_t bin_num = this->data_.bin_num;
  this->data_ = AudioData{};
  this->data_.max_vol = max_vol;
  this->data_.bin_num = bin_num;
}

void AudioProcessor::set_peak_controls(uint8_t max_vol, uint8_t bin_num) {
  this->data_.max_vol = max_vol;
  this->data_.bin_num = bin_num;
}

// ---------------------------------------------------------------------------
// Pre-filtering
// ---------------------------------------------------------------------------

void AudioProcessor::run_mic_filter_() {
  // Band pass, 90 Hz to 20 kHz. Upstream constants, float path.
  constexpr float alpha = 0.0256f;  // 90 Hz
  constexpr float beta1 = 0.85f;    // 20 kHz
  constexpr float beta2 = (1.0f - beta1) / 2.0f;

  for (uint16_t i = 0; i < SAMPLES_FFT; i++) {
    // FIR lowpass, to remove high frequency noise.
    float high_filtered_sample;
    if (i < (SAMPLES_FFT - 1))
      high_filtered_sample = beta1 * this->fft_[i] + beta2 * this->filter_last_vals_[0] + beta2 * this->fft_[i + 1];
    else
      high_filtered_sample =
          beta1 * this->fft_[i] + beta2 * this->filter_last_vals_[0] + beta2 * this->filter_last_vals_[1];
    this->filter_last_vals_[1] = this->filter_last_vals_[0];
    this->filter_last_vals_[0] = this->fft_[i];
    this->fft_[i] = high_filtered_sample;
    // IIR highpass, to remove low frequency noise.
    this->filter_lowfilt_ += alpha * (this->fft_[i] - this->filter_lowfilt_);
    this->fft_[i] = this->fft_[i] - this->filter_lowfilt_;
  }
}

// ---------------------------------------------------------------------------
// FFT
// ---------------------------------------------------------------------------

void AudioProcessor::run_fft_(uint32_t now_ms) {
  (void) now_ms;

  // Remove the DC offset.
  float sum = 0.0f;
  for (uint16_t i = 0; i < SAMPLES_FFT; i++)
    sum += this->fft_[i];
  const float mean = sum / static_cast<float>(SAMPLES_FFT);

  uint16_t crossings = 0;
  float previous = 0.0f;
  for (uint16_t i = 0; i < SAMPLES_FFT; i++) {
    this->fft_[i] -= mean;
    if (i > 0 && ((previous < 0.0f && this->fft_[i] >= 0.0f) || (previous >= 0.0f && this->fft_[i] < 0.0f)))
      crossings++;
    previous = this->fft_[i];
  }
  // MoonModules u_data[11]: zero crossings over the current batch of samples.
  this->data_.zero_crossing_count = crossings;

  // Apply the window and fill the buffer with interleaved complex values, back
  // to front so no sample is overwritten before it is read.
  for (int i = SAMPLES_FFT - 1; i >= 0; i--) {
    const float windowed_sample = this->fft_[i] * this->window_[i];
    this->fft_[i * 2] = windowed_sample;
    this->fft_[i * 2 + 1] = 0.0f;
  }

  fft_forward(this->fft_, SAMPLES_FFT);

  this->fft_[0] = 0.0f;  // the DC bin is not needed and causes a strong spike

  // Convert to magnitude and find the dominant bin. Writing the magnitude to
  // fft_[i] while reading fft_[2i] and fft_[2i + 1] is safe, and is what
  // upstream does.
  const float hz_per_bin = this->bin_width();
  this->fft_major_peak_ = 0.0f;
  this->fft_magnitude_ = 0.0f;
  for (uint16_t i = 1; i < SAMPLES_FFT_2; i++) {
    const float real_part = this->fft_[i * 2];
    const float imag_part = this->fft_[i * 2 + 1];
    this->fft_[i] = sqrtf(real_part * real_part + imag_part * imag_part);
    if (this->fft_[i] > this->fft_magnitude_) {
      this->fft_magnitude_ = this->fft_[i];
      this->fft_major_peak_ = i * hz_per_bin;
    }
  }
  // Restrict to the range the effects expect.
  this->fft_major_peak_ = constrain(this->fft_major_peak_, 1.0f, MAX_FREQUENCY);
}

float AudioProcessor::fft_add_avg_(uint16_t from, uint16_t to) const {
  float result = 0.0f;
  for (uint16_t i = from; i <= to; i++)
    result += this->fft_[i];
  // Divide by 16 to reduce magnitude; the end result should be scaled linear
  // and about 4096 max.
  result = result * 0.0625f;
  return result / static_cast<float>(to - from + 1);
}

void AudioProcessor::map_frequency_channels_() {
  if (fabsf(this->sample_avg_) > 0.5f) {  // noise gate open
    for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++)
      this->fft_calc_[i] = this->fft_add_avg_(this->bin_from_[i], this->bin_to_[i]) * this->bin_damp_[i];
  } else {  // noise gate closed, just decay the old values
    for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++) {
      this->fft_calc_[i] *= 0.85f;
      if (this->fft_calc_[i] < 4.0f)
        this->fft_calc_[i] = 0.0f;
    }
  }
}

void AudioProcessor::post_process_fft_results_(bool noise_gate_open) {
  const uint8_t scaling = this->config_.scaling;
  const uint8_t agc = this->config_.agc;

  for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++) {
    if (noise_gate_open) {
      // Adjustment for frequency curves.
      this->fft_calc_[i] *= FFT_RESULT_PINK[i];
      if (scaling > 0)
        this->fft_calc_[i] *= FFT_DOWNSCALE;  // adjustment related to the FFT window
      // Manual linear adjustment of gain for different input types.
      this->fft_calc_[i] *= agc ? this->mult_agc_
                                : (static_cast<float>(this->config_.gain) / 40.0f *
                                       static_cast<float>(this->config_.input_level) / 128.0f +
                                   1.0f / 16.0f);
      if (this->fft_calc_[i] < 0)
        this->fft_calc_[i] = 0;
    }

    // Smooth the results: rise fast, fall slower.
    if (this->fft_calc_[i] > this->fft_avg_[i]) {
      this->fft_avg_[i] = this->fft_calc_[i] * 0.75f + 0.25f * this->fft_avg_[i];
    } else {
      const uint16_t decay = this->config_.decay_ms;
      if (decay < 1000)
        this->fft_avg_[i] = this->fft_calc_[i] * 0.22f + 0.78f * this->fft_avg_[i];
      else if (decay < 2000)
        this->fft_avg_[i] = this->fft_calc_[i] * 0.17f + 0.83f * this->fft_avg_[i];
      else if (decay < 3000)
        this->fft_avg_[i] = this->fft_calc_[i] * 0.14f + 0.86f * this->fft_avg_[i];
      else
        this->fft_avg_[i] = this->fft_calc_[i] * 0.1f + 0.9f * this->fft_avg_[i];
    }

    this->fft_calc_[i] = constrain(this->fft_calc_[i], 0.0f, 1023.0f);
    this->fft_avg_[i] = constrain(this->fft_avg_[i], 0.0f, 1023.0f);

    float current_result = this->config_.limiter ? this->fft_avg_[i] : this->fft_calc_[i];

    switch (scaling) {
      case FFT_SCALE_LOG:
        current_result *= 0.42f;
        current_result -= 8.0f;  // this skips the lowest row, giving room for peaks
        if (current_result > 1.0f)
          current_result = logf(current_result);
        else
          current_result = 0.0f;  // log(1) = 0 and log(0) is undefined
        current_result *= 0.85f + (static_cast<float>(i) / 18.0f);
        current_result = mapf(current_result, 0, LOG_256, 0, 255);
        break;
      case FFT_SCALE_LINEAR:
        current_result *= 0.30f;
        current_result -= 4.0f;
        if (current_result < 1.0f)
          current_result = 0.0f;
        current_result *= 0.85f + (static_cast<float>(i) / 1.8f);
        break;
      case FFT_SCALE_SQRT:
        current_result *= 0.38f;
        current_result -= 6.0f;
        if (current_result > 1.0f)
          current_result = sqrtf(current_result);
        else
          current_result = 0.0f;
        current_result *= 0.85f + (static_cast<float>(i) / 4.5f);
        current_result = mapf(current_result, 0.0f, 16.0f, 0.0f, 255.0f);
        break;
      case FFT_SCALE_NONE:
      default:
        current_result -= 4;
        break;
    }

    if (agc > 0) {  // apply the extra "GEQ Gain" if the user set one
      float post_gain = static_cast<float>(this->config_.input_level) / 128.0f;
      if (post_gain < 1.0f)
        post_gain = ((post_gain - 1.0f) * 0.8f) + 1.0f;
      current_result *= post_gain;
    }
    this->data_.fft_result[i] = static_cast<uint8_t>(constrain(static_cast<int>(current_result), 0, 255));
  }
}

// ---------------------------------------------------------------------------
// Peak detection
// ---------------------------------------------------------------------------

void AudioProcessor::detect_sample_peak_(uint32_t now_ms) {
  /* Upstream's own note: this continuously triggers while the amplitude in the
   * selected bin is above a threshold, so it detects high activity in a
   * frequency bin rather than a peak. */
  if ((this->sample_avg_ > 1) && (this->data_.max_vol > 0) && (this->data_.bin_num > 4) &&
      (this->fft_[this->data_.bin_num] > this->data_.max_vol) && ((now_ms - this->time_of_peak_) > 100)) {
    this->sample_peak_ = true;
    this->time_of_peak_ = now_ms;
  }
}

void AudioProcessor::auto_reset_peak_(uint32_t now_ms) {
  /* Upstream uses max(50, strip.getFrameTime()). There is no strip here and the
   * front ends run at 23 ms, so the lower bound is always the one that applies. */
  constexpr uint32_t PEAK_DELAY = 50;
  if (now_ms - this->time_of_peak_ > PEAK_DELAY)
    this->sample_peak_ = false;
}

// ---------------------------------------------------------------------------
// Volume filters and AGC
// ---------------------------------------------------------------------------

void AudioProcessor::get_sample_(uint32_t now_ms) {
  const float weighting = 0.2f;  // exponential filter weighting
  const int agc_preset = (this->config_.agc > 0) ? (this->config_.agc - 1) : 0;

  const int mic_in = static_cast<int>(this->mic_data_real_);

  this->mic_lev_ += (this->mic_data_real_ - this->mic_lev_) / 12288.0;
  if (mic_in < this->mic_lev_)
    this->mic_lev_ = ((this->mic_lev_ * 31.0) + this->mic_data_real_) / 32.0;  // align to the lowest input signal

  // Exponential filter to smooth the signal out.
  const float mic_in_no_dc = fabsf(this->mic_data_real_ - static_cast<float>(this->mic_lev_));
  this->exp_adj_f_ = (weighting * mic_in_no_dc + (1.0f - weighting) * this->exp_adj_f_);
  this->exp_adj_f_ = fabsf(this->exp_adj_f_);

  this->exp_adj_f_ = (this->exp_adj_f_ <= this->config_.squelch) ? 0 : this->exp_adj_f_;  // simple noise gate
  if ((this->config_.squelch == 0) && (this->exp_adj_f_ < 0.25f))
    this->exp_adj_f_ = 0;  // do something meaningful when squelch is 0

  const float tmp_sample = this->exp_adj_f_;

  float sample_adj = tmp_sample * this->config_.gain / 40.0f * this->config_.input_level / 128.0f + tmp_sample / 16.0f;
  this->sample_real_ = tmp_sample;

  sample_adj = fmaxf(fminf(sample_adj, 255.0f), 0.0f);
  this->sample_raw_ = static_cast<int16_t>(sample_adj);

  // Keep the peak sample, but decay it when the current sample is below it.
  if ((this->sample_max_ < this->sample_real_) && (this->sample_real_ > 0.5f)) {
    this->sample_max_ = this->sample_max_ + 0.5 * (this->sample_real_ - this->sample_max_);
    /* Another simple way to detect samplePeak: this cannot detect beats, but it
     * reacts to peak volume. */
    if (((this->data_.bin_num < 12) || (this->data_.max_vol < 1)) && (now_ms - this->time_of_peak_ > 80) &&
        (this->sample_avg_ > 1)) {
      this->sample_peak_ = true;
      this->time_of_peak_ = now_ms;
    }
  } else {
    if ((this->mult_agc_ * this->sample_max_ > AGC_ZONE_STOP[agc_preset]) && (this->config_.agc > 0))
      this->sample_max_ += 0.5 * (this->sample_real_ - this->sample_max_);  // over the AGC zone, get back quickly
    else
      this->sample_max_ *= AGC_SAMPLE_DECAY[agc_preset];  // signal to zero over 5 to 8 seconds
  }
  if (this->sample_max_ < 0.5)
    this->sample_max_ = 0.0;

  this->sample_avg_ = ((this->sample_avg_ * 15.0f) + sample_adj) / 16.0f;  // smooth over the last 16 samples
  this->sample_avg_ = fabsf(this->sample_avg_);
}

void AudioProcessor::agc_avg_(uint32_t the_time, uint32_t now_ms) {
  const int agc_preset = (this->config_.agc > 0) ? (this->config_.agc - 1) : 0;

  float last_mult_agc = this->mult_agc_;
  float mult_agc_temp = this->mult_agc_;
  float tmp_agc = this->sample_real_ * this->mult_agc_;

  if (this->last_sound_agc_ != static_cast<int>(this->config_.agc))
    this->control_integrated_ = 0.0;  // new preset, reset the integrator

  /* The PI controller needs a constant "frequency", so the control loop must not
   * run at an insane speed. */
  uint32_t time_now = now_ms;
  if ((the_time > 0) && (the_time < time_now))
    time_now = the_time;  // the caller may override the clock

  if (time_now - this->last_agc_time_ > 2) {
    this->last_agc_time_ = time_now;

    if ((fabsf(this->sample_real_) < 2.0f) || (this->sample_max_ < 1.0)) {
      // The microphone signal is squelched, deliver silence.
      tmp_agc = 0;
      // Spin the integrated error buffer down.
      if (fabs(this->control_integrated_) < 0.01)
        this->control_integrated_ = 0.0;
      else
        this->control_integrated_ *= 0.91;
    } else {
      // Compute the new setpoint.
      if (tmp_agc <= AGC_TARGET0_UP[agc_preset])
        mult_agc_temp = AGC_TARGET0[agc_preset] / static_cast<float>(this->sample_max_);
      else
        mult_agc_temp = AGC_TARGET1[agc_preset] / static_cast<float>(this->sample_max_);
    }
    // Limit the amplification.
    if (mult_agc_temp > 32.0f)
      mult_agc_temp = 32.0f;
    if (mult_agc_temp < 1.0f / 64.0f)
      mult_agc_temp = 1.0f / 64.0f;

    const float control_error = mult_agc_temp - last_mult_agc;

    if (((mult_agc_temp > 0.085f) && (mult_agc_temp < 6.5f))  // integrator anti-windup by clamping
        && (this->mult_agc_ * this->sample_max_ < AGC_ZONE_STOP[agc_preset]))  // integrator ceiling
      this->control_integrated_ += control_error * 0.002 * 0.25;  // 2 ms integration time, 0.25 for damping
    else
      this->control_integrated_ *= 0.9;

    // Apply PI control, checking the zone of the signal with the previous gain.
    tmp_agc = this->sample_real_ * last_mult_agc;
    if ((tmp_agc > AGC_ZONE_HIGH[agc_preset]) ||
        (tmp_agc < this->config_.squelch + AGC_ZONE_LOW[agc_preset])) {  // upper or lower energy zone
      mult_agc_temp = last_mult_agc + AGC_FOLLOW_FAST[agc_preset] * AGC_CONTROL_KP[agc_preset] * control_error;
      mult_agc_temp += AGC_FOLLOW_FAST[agc_preset] * AGC_CONTROL_KI[agc_preset] * this->control_integrated_;
    } else {  // normal zone
      mult_agc_temp = last_mult_agc + AGC_FOLLOW_SLOW[agc_preset] * AGC_CONTROL_KP[agc_preset] * control_error;
      mult_agc_temp += AGC_FOLLOW_SLOW[agc_preset] * AGC_CONTROL_KI[agc_preset] * this->control_integrated_;
    }

    // Limit the amplification again; the PI controller sometimes overshoots.
    if (mult_agc_temp > 32.0f)
      mult_agc_temp = 32.0f;
    if (mult_agc_temp < 1.0f / 64.0f)
      mult_agc_temp = 1.0f / 64.0f;
  }

  // Now finally amplify the signal.
  tmp_agc = this->sample_real_ * mult_agc_temp;
  if (fabsf(this->sample_real_) < 2.0f)
    tmp_agc = 0.0f;  // squelch threshold
  if (tmp_agc > 255)
    tmp_agc = 255.0f;
  if (tmp_agc < 1)
    tmp_agc = 0.0f;

  this->mult_agc_ = mult_agc_temp;
  this->raw_sample_agc_ = static_cast<int16_t>(0.8f * tmp_agc + 0.2f * static_cast<float>(this->raw_sample_agc_));
  if (fabsf(tmp_agc) < 1.0f)
    this->sample_agc_ = 0.5f * tmp_agc + 0.5f * this->sample_agc_;  // fast path to zero
  else
    this->sample_agc_ += AGC_SAMPLE_SMOOTH[agc_preset] * (tmp_agc - this->sample_agc_);

  this->sample_agc_ = fabsf(this->sample_agc_);
  this->last_sound_agc_ = static_cast<int>(this->config_.agc);
}

void AudioProcessor::limit_sample_dynamics_(uint32_t now_ms) {
  const float big_change = 196;  // a large, expected sample value

  if (!this->config_.limiter)
    return;

  long delta_time = static_cast<long>(now_ms - this->last_limiter_time_);
  delta_time = constrain(delta_time, 1L, 1000L);
  float delta_sample = this->data_.volume_smth - this->last_volume_smth_;

  if (this->config_.attack_ms > 0) {
    const float max_attack = big_change * static_cast<float>(delta_time) / static_cast<float>(this->config_.attack_ms);
    if (delta_sample > max_attack)
      delta_sample = max_attack;
  }
  if (this->config_.decay_ms > 0) {
    const float max_decay = -big_change * static_cast<float>(delta_time) / static_cast<float>(this->config_.decay_ms);
    if (delta_sample < max_decay)
      delta_sample = max_decay;
  }

  this->data_.volume_smth = this->last_volume_smth_ + delta_sample;
  this->last_volume_smth_ = this->data_.volume_smth;
  this->last_limiter_time_ = now_ms;
}

// ---------------------------------------------------------------------------
// One block
// ---------------------------------------------------------------------------

void AudioProcessor::process_block(const int16_t *samples, uint32_t now_ms) {
  if (!this->ready_ || samples == nullptr)
    return;

  for (uint16_t i = 0; i < SAMPLES_FFT; i++)
    this->fft_[i] = static_cast<float>(samples[i]);

  if (this->config_.mic_filter)
    this->run_mic_filter_();

  // Find the highest sample in the batch, skipping extreme values, which are
  // normally artefacts.
  float max_sample = 0.0f;
  for (uint16_t i = 0; i < SAMPLES_FFT; i++) {
    const float value = this->fft_[i];
    if ((value <= (32767.0f - 1024.0f)) && (value >= (-32768.0f + 1024.0f))) {
      const float magnitude = fabsf(value);
      if (magnitude > max_sample)
        max_sample = magnitude;
    }
  }
  this->mic_data_real_ = max_sample;

  /* WLED runs getSample() and agcAvg() from its main loop, hundreds of times per
   * FFT batch, with micDataReal held constant in between, and compensates for a
   * delayed loop by re-running the pair in 2 ms steps. Here the whole pipeline is
   * on one task, so the same catch-up loop reproduces that cadence exactly,
   * without the data race upstream lives with. */
  int loop_delay = static_cast<int>(now_ms - this->last_um_run_);
  if (this->last_um_run_ == 0)
    loop_delay = 0;  // startup, no valid data from the last run
  if (loop_delay < 2)
    loop_delay = 0;
  if (loop_delay > 200)
    loop_delay = 200;
  do {
    this->get_sample_(now_ms);
    const uint32_t stepped = (now_ms > static_cast<uint32_t>(loop_delay)) ? now_ms - loop_delay : 0;
    this->agc_avg_(stepped, now_ms);
    loop_delay -= 2;
  } while (loop_delay > 0);
  this->last_um_run_ = now_ms;

  if (this->sample_avg_ > 0.25f) {  // the noise gate is open, so the FFT results will be used
    this->run_fft_(now_ms);
  } else {
    // Noise gate closed, so the FFT was skipped. Clear the results.
    std::memset(this->fft_, 0, SAMPLES_FFT * sizeof(float));
    this->fft_major_peak_ = 1;
    this->fft_magnitude_ = 0.001f;
    this->data_.zero_crossing_count = 0;
  }

  this->map_frequency_channels_();
  this->post_process_fft_results_(fabsf(this->sample_avg_) > 0.25f);

  this->auto_reset_peak_(now_ms);
  this->detect_sample_peak_(now_ms);

  // Update the samples the effects read.
  this->data_.volume_smth = this->config_.agc ? this->sample_agc_ : this->sample_avg_;
  const int16_t raw = this->config_.agc ? this->raw_sample_agc_ : this->sample_raw_;
  this->data_.volume_raw = static_cast<uint16_t>(raw < 0 ? 0 : raw);
  this->data_.my_magnitude = this->fft_magnitude_;
  if (this->config_.agc)
    this->data_.my_magnitude *= this->mult_agc_;
  if (this->data_.volume_smth < 1)
    this->data_.my_magnitude = 0.001f;  // noise gate closed, mute
  this->data_.fft_major_peak = this->fft_major_peak_;
  this->data_.sample_peak = this->sample_peak_ ? 1 : 0;

  this->limit_sample_dynamics_(now_ms);

  /* The MoonModules extras. WLED-MM's own util.cpp aliases soundPressure and
   * agcSensitivity onto the volume it already has, so this does the same rather
   * than invent numbers, and publishes a real zero crossing count. */
  this->data_.fft_major_peak_smth += 0.2f * (this->fft_major_peak_ - this->data_.fft_major_peak_smth);
  this->data_.sound_pressure = this->data_.volume_smth;
  this->data_.agc_sensitivity = this->config_.agc ? constrain(this->mult_agc_ * 8.0f, 0.0f, 255.0f)
                                                  : this->data_.volume_smth;
}

}  // namespace wled_fx
}  // namespace esphome
