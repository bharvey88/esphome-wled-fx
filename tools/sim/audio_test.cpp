// Host tests for the audio processing core.
//
// The core is framework free, so the same code the firmware runs can be fed
// synthetic PCM here and checked against known answers: a 1 kHz sine, a sweep,
// a kick-drum-like pulse train, silence, and the same tone at other sample
// rates. This is the only place the pipeline gets checked without hardware.
//
// Usage: wled_fx_audio_test [--verbose]

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

#include "../../components/wled_fx/wf_audio_core.h"
#include "../../components/wled_fx/wf_fft.h"

using namespace esphome::wled_fx;

namespace {

bool g_verbose = false;
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string &what) {
  g_checks++;
  if (!ok) {
    g_failures++;
    printf("  FAIL  %s\n", what.c_str());
  } else if (g_verbose) {
    printf("  ok    %s\n", what.c_str());
  }
}

void check_close(float value, float expected, float tolerance, const std::string &what) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s (got %.3f, expected %.3f +/- %.3f)", what.c_str(), value, expected, tolerance);
  check(fabsf(value - expected) <= tolerance, buffer);
}

void check_range(float value, float low, float high, const std::string &what) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s (got %.3f, wanted %.3f to %.3f)", what.c_str(), value, low, high);
  check(value >= low && value <= high, buffer);
}

constexpr float PI_F = 3.14159265358979323846f;

/* Drives the processor with generated audio. The generator is called per sample
 * and gets the absolute sample index, so a signal can run across block
 * boundaries without a discontinuity. */
struct Harness {
  AudioProcessor processor;
  uint32_t sample_rate;
  uint32_t now_ms{1000};
  uint64_t sample_index{0};
  double now_fractional{1000.0};
  int blocks_with_peak{0};
  int blocks_run{0};
  double total_core_us{0.0};

  explicit Harness(const AudioConfig &config) : sample_rate(config.sample_rate) {
    processor.set_config(config);
    if (!processor.begin()) {
      printf("FATAL: FFT backend refused to initialise\n");
      exit(2);
    }
  }

  template<typename Gen> void run(float seconds, Gen generator) {
    const double block_ms = 1000.0 * AudioProcessor::SAMPLES_FFT / static_cast<double>(sample_rate);
    const int blocks = static_cast<int>(seconds * 1000.0 / block_ms);
    int16_t block[AudioProcessor::SAMPLES_FFT];
    for (int b = 0; b < blocks; b++) {
      for (uint16_t i = 0; i < AudioProcessor::SAMPLES_FFT; i++) {
        const float value = generator(sample_index, sample_rate);
        const float clipped = value > 32767.0f ? 32767.0f : (value < -32768.0f ? -32768.0f : value);
        block[i] = static_cast<int16_t>(lrintf(clipped));
        sample_index++;
      }
      now_fractional += block_ms;
      now_ms = static_cast<uint32_t>(now_fractional);
      const auto started = std::chrono::steady_clock::now();
      processor.process_block(block, now_ms);
      const auto ended = std::chrono::steady_clock::now();
      total_core_us += std::chrono::duration<double, std::micro>(ended - started).count();
      blocks_run++;
      if (processor.data().sample_peak)
        blocks_with_peak++;
    }
  }

  const AudioData &data() const { return processor.data(); }

  uint8_t dominant_band() const {
    uint8_t best = 0;
    for (uint8_t i = 1; i < NUM_GEQ_CHANNELS; i++) {
      if (data().fft_result[i] > data().fft_result[best])
        best = i;
    }
    return best;
  }

  void dump(const char *label) const {
    if (!g_verbose)
      return;
    printf("  %-16s volume_smth %6.1f  raw %4u  peak %u  major %8.1f Hz  magnitude %10.1f\n", label,
           data().volume_smth, data().volume_raw, data().sample_peak, data().fft_major_peak, data().my_magnitude);
    printf("  %-16s geq", "");
    for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++)
      printf(" %3u", data().fft_result[i]);
    printf("\n");
  }
};

AudioConfig base_config(uint32_t sample_rate = 22050, uint8_t agc = AGC_OFF) {
  AudioConfig config;
  config.sample_rate = sample_rate;
  config.agc = agc;
  config.gain = 60;
  config.squelch = 10;
  config.input_level = 128;
  config.scaling = FFT_SCALE_SQRT;
  config.limiter = true;
  return config;
}

// A steady sine of the given frequency and amplitude in int16 units.
auto sine(float frequency, float amplitude) {
  return [frequency, amplitude](uint64_t index, uint32_t rate) {
    return amplitude * sinf(2.0f * PI_F * frequency * static_cast<float>(index) / static_cast<float>(rate));
  };
}

// ---------------------------------------------------------------------------

void test_silence() {
  printf("silence\n");
  Harness harness(base_config());
  harness.run(2.0f, [](uint64_t, uint32_t) { return 0.0f; });
  harness.dump("digital zero");

  check(harness.data().volume_smth < 1.0f, "volume_smth stays under 1");
  check(harness.data().volume_raw == 0, "volume_raw is zero");
  check(harness.data().sample_peak == 0, "no sample peak");
  check_close(harness.data().fft_major_peak, 1.0f, 0.001f, "fft_major_peak is clamped to 1 Hz");
  check(harness.data().my_magnitude <= 0.001f, "my_magnitude is muted");
  bool all_zero = true;
  for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++)
    all_zero = all_zero && harness.data().fft_result[i] == 0;
  check(all_zero, "every GEQ channel is zero");

  // A tone below the squelch must be treated exactly like silence.
  printf("below squelch\n");
  Harness quiet(base_config());
  quiet.run(2.0f, sine(1000.0f, 5.0f));
  quiet.dump("amplitude 5");
  check(quiet.data().volume_smth < 1.0f, "a signal under the squelch stays under 1");
  bool quiet_zero = true;
  for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++)
    quiet_zero = quiet_zero && quiet.data().fft_result[i] == 0;
  check(quiet_zero, "every GEQ channel is zero below the squelch");
}

void test_sine_1khz() {
  printf("1 kHz sine at 22050 Hz\n");
  Harness harness(base_config());
  harness.run(2.0f, sine(1000.0f, 60.0f));
  harness.dump("1 kHz");

  const float bin_width = 22050.0f / AudioProcessor::SAMPLES_FFT;
  check(harness.dominant_band() == 7, "band 7 (818 to 1120 Hz) is the loudest channel");
  check_close(harness.data().fft_major_peak, 1000.0f, bin_width, "fft_major_peak is within one bin of 1 kHz");
  check_range(harness.data().volume_smth, 1.0f, 255.0f, "volume_smth is inside the 0 to 255 contract");
  check_range(static_cast<float>(harness.data().volume_raw), 0.0f, 255.0f,
              "volume_raw keeps WLED's 0 to 255 range");
  check(harness.data().fft_result[7] > 40, "band 7 is clearly lit");
  for (uint8_t i = 0; i < NUM_GEQ_CHANNELS; i++) {
    if (i == 7)
      continue;
    check(harness.data().fft_result[i] < harness.data().fft_result[7],
          "band " + std::to_string(i) + " is below band 7");
  }

  /* my_magnitude has to land where the effects expect it. Rocktaves reads
   * my_magnitude / 16, squelches below 48 and saturates above 144, so the raw
   * value has a useful window of roughly 768 to 2304. WLED's usermod feeds the
   * FFT with samples in int16 units, exactly like MicrophoneSource does here, so
   * matching the window is a matter of matching the scale: an unwindowed peak of
   * A * N / 2 times the Blackman-Harris coherent gain of 0.35875.
   *
   * The squelch is off for this one. Upstream's noise gate zeroes the filter
   * state itself rather than its output, so a signal has to be about five times
   * the squelch before anything passes at all, which would hide the quiet
   * amplitude this window needs. */
  const float amplitude = 16.0f;
  AudioConfig open_gate = base_config();
  open_gate.squelch = 0;
  Harness scaled(open_gate);
  scaled.run(2.0f, sine(1000.0f, amplitude));
  scaled.dump("amplitude 16");
  const float expected = amplitude * AudioProcessor::SAMPLES_FFT * 0.35875f / 2.0f;
  check_close(scaled.data().my_magnitude, expected, expected * 0.15f,
              "my_magnitude matches A * N * 0.35875 / 2");
  check_range(scaled.data().my_magnitude / 16.0f, 48.0f, 144.0f,
              "my_magnitude / 16 sits inside the window Rocktaves uses");
}

void test_sweep() {
  printf("sine sweep at 22050 Hz\n");
  struct Point {
    float frequency;
    int expected_band;  // -1 means "somewhere in the bass", the low bands overlap
  };
  const Point POINTS[] = {
      {150.0f, -1},   {350.0f, 4},    {1000.0f, 7},  {2100.0f, 10},
      {4000.0f, 13},  {6000.0f, 14},  {8500.0f, 15},
  };
  const float bin_width = 22050.0f / AudioProcessor::SAMPLES_FFT;

  int previous_band = -1;
  for (const auto &point : POINTS) {
    Harness harness(base_config());
    harness.run(1.5f, sine(point.frequency, 60.0f));
    char label[64];
    snprintf(label, sizeof(label), "%.0f Hz", point.frequency);
    harness.dump(label);

    const int band = harness.dominant_band();
    check_close(harness.data().fft_major_peak, point.frequency, bin_width,
                std::string(label) + ": fft_major_peak is within one bin");
    if (point.expected_band >= 0) {
      check(band == point.expected_band,
            std::string(label) + ": lights band " + std::to_string(point.expected_band) + ", got " +
                std::to_string(band));
    } else {
      check(band <= 3, std::string(label) + ": lights one of the bass bands, got " + std::to_string(band));
    }
    check(band >= previous_band, std::string(label) + ": the lit band does not move backwards");
    previous_band = band;
  }
}

void test_sample_rates() {
  printf("1 kHz sine at other sample rates\n");
  for (uint32_t rate : {16000u, 32000u, 44100u}) {
    Harness harness(base_config(rate));
    harness.run(2.0f, sine(1000.0f, 60.0f));
    char label[64];
    snprintf(label, sizeof(label), "%u Hz", rate);
    harness.dump(label);

    const float bin_width = static_cast<float>(rate) / AudioProcessor::SAMPLES_FFT;
    check_close(harness.data().fft_major_peak, 1000.0f, bin_width,
                std::string(label) + ": fft_major_peak is within one bin of 1 kHz");
    check(harness.dominant_band() == 7,
          std::string(label) + ": the rescaled bin mapping still puts 1 kHz in band 7, got " +
              std::to_string(harness.dominant_band()));
  }
}

void test_kick_pulses() {
  printf("kick drum pulse train\n");
  AudioConfig config = base_config();
  Harness harness(config);

  /* A 60 Hz burst with a sharp attack and a 60 ms exponential decay, twice a
   * second, silence in between. */
  const float period_s = 0.5f;
  const float decay_s = 0.06f;
  harness.run(4.0f, [period_s, decay_s](uint64_t index, uint32_t rate) {
    const float t = static_cast<float>(index) / static_cast<float>(rate);
    const float phase = fmodf(t, period_s);
    const float envelope = expf(-phase / decay_s);
    return 9000.0f * envelope * sinf(2.0f * PI_F * 60.0f * phase);
  });
  harness.dump("kick train");

  // Eight kicks in four seconds, and the flag auto-resets after 50 ms, so it
  // should be set in a handful of blocks and clear in most of them.
  printf("  %d of %d blocks reported a sample peak\n", harness.blocks_with_peak, harness.blocks_run);
  check(harness.blocks_with_peak >= 6, "the pulse train fires sample_peak at least once per kick");
  check(harness.blocks_with_peak < harness.blocks_run / 2, "sample_peak is not stuck on");

  // Silence afterwards must clear it again.
  harness.run(1.0f, [](uint64_t, uint32_t) { return 0.0f; });
  check(harness.data().sample_peak == 0, "sample_peak clears once the kicks stop");
}

void test_agc() {
  printf("automatic gain control\n");
  // A modest tone, comfortably above the noise gate. With AGC off the gain never
  // moves; with AGC on the PI controller should wind it up towards its setpoint.
  /* Upstream runs the AGC controller whatever the setting is and only decides at
   * the end whether to use its output, so the multiplier moving with AGC off is
   * correct. What matters is that the published volume ignores it: with gain 60
   * and input level 128 it must be the plain sampleAdj, amplitude * 1.5625. */
  /* Two seconds only: micLev is a very slow leveller that erodes a constant
   * tone over tens of seconds, which real audio never is. */
  Harness plain(base_config(22050, AGC_OFF));
  plain.run(2.0f, sine(1000.0f, 120.0f));
  plain.dump("agc off");
  check_close(plain.data().volume_smth, 120.0f * 1.5625f, 120.0f * 1.5625f * 0.2f,
              "with AGC off the volume is the manual gain only");

  for (uint8_t preset : {AGC_NORMAL, AGC_VIVID, AGC_LAZY}) {
    Harness harness(base_config(22050, preset));
    harness.run(20.0f, sine(1000.0f, 120.0f));
    char label[64];
    snprintf(label, sizeof(label), "agc preset %u, quiet", preset);
    harness.dump(label);
    check(harness.processor.agc_multiplier() > 1.1f,
          std::string(label) + ": the multiplier wound up, got " +
              std::to_string(harness.processor.agc_multiplier()));
    check_range(harness.data().volume_smth, 80.0f, 255.0f, std::string(label) + ": volume_smth is amplified");
    check_range(harness.data().agc_sensitivity, 0.0f, 255.0f, std::string(label) + ": agc_sensitivity is in range");
  }

  // The other direction: a loud signal has to be pulled back down.
  for (uint8_t preset : {AGC_NORMAL, AGC_VIVID, AGC_LAZY}) {
    Harness harness(base_config(22050, preset));
    harness.run(20.0f, sine(1000.0f, 6000.0f));
    char label[64];
    snprintf(label, sizeof(label), "agc preset %u, loud", preset);
    harness.dump(label);
    check(harness.processor.agc_multiplier() < 0.2f,
          std::string(label) + ": the multiplier wound down, got " +
              std::to_string(harness.processor.agc_multiplier()));
    check_range(harness.data().volume_smth, 0.0f, 255.0f, std::string(label) + ": volume_smth stays in range");
  }
}

void test_effect_owned_controls() {
  printf("effect owned max_vol and bin_num\n");
  Harness harness(base_config());
  harness.run(0.5f, sine(1000.0f, 60.0f));
  check(harness.data().max_vol == 31, "max_vol keeps its default until an effect writes it");
  check(harness.data().bin_num == 8, "bin_num keeps its default until an effect writes it");

  // What an effect such as Waterfall does every frame.
  harness.processor.set_peak_controls(120, 20);
  harness.run(0.5f, sine(1000.0f, 60.0f));
  check(harness.data().max_vol == 120, "a block does not overwrite the effect's max_vol");
  check(harness.data().bin_num == 20, "a block does not overwrite the effect's bin_num");
}

void test_contract_invariants() {
  printf("contract invariants over a mixed signal\n");
  Harness harness(base_config(22050, AGC_NORMAL));
  float worst_peak_low = 1e9f;
  float worst_volume = -1.0f;
  int raw_out_of_range = 0;

  for (int round = 0; round < 6; round++) {
    const float frequency = 80.0f * static_cast<float>(round + 1);
    const float amplitude = (round % 2 == 0) ? 3000.0f : 3.0f;  // loud, then under the squelch
    harness.run(1.0f, sine(frequency, amplitude));
    const AudioData &data = harness.data();
    if (data.fft_major_peak < worst_peak_low)
      worst_peak_low = data.fft_major_peak;
    if (data.volume_smth > worst_volume)
      worst_volume = data.volume_smth;
    if (data.volume_raw > 255)
      raw_out_of_range++;
  }

  check(worst_peak_low >= 1.0f, "fft_major_peak never drops below 1 Hz, even in the quiet rounds");
  check_range(worst_volume, 0.0f, 255.0f, "volume_smth never leaves 0 to 255");
  check(raw_out_of_range == 0, "volume_raw never leaves the int16 0 to 255 range WLED uses");
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--verbose") == 0)
      g_verbose = true;
  }

  printf("wled_fx audio core tests, FFT backend: %s\n\n", fft_backend_name());

  test_silence();
  test_sine_1khz();
  test_sweep();
  test_sample_rates();
  test_kick_pulses();
  test_agc();
  test_effect_owned_controls();
  test_contract_invariants();

  // A rough cost figure. The host is not an ESP32, but it tells us whether the
  // pipeline is in the right order of magnitude.
  {
    Harness timing(base_config());
    timing.run(5.0f, sine(1000.0f, 3000.0f));
    printf("\ncore cost: %.1f us per 512 sample block on this host over %d blocks\n",
           timing.total_core_us / timing.blocks_run, timing.blocks_run);
  }

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
