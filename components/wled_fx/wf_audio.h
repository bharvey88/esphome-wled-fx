#pragma once

/* The audio contract that audio-reactive effects read, plus WLED's simulated
 * sound so those effects run before a microphone exists.
 *
 * Derived from WLED 16.0.1 wled00/fcn_declare.h (the um_data_t layout),
 * wled00/util.cpp (simulateSound) and usermods/audioreactive/audio_reactive.cpp.
 * The MoonModules extras are derived from WLED-MM wled00/util.cpp.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * WLED hands effects a `um_data_t`, a tagged array of void pointers that every
 * effect has to index and cast by hand. That indirection buys nothing here, so
 * this port flattens it into a plain struct with the same field meanings and the
 * same order. The mechanical transform for an effect body is in PORTING.md.
 */

#include <cstdint>

namespace esphome {
namespace wled_fx {

// WLED's NUM_GEQ_CHANNELS. The GEQ bin count is baked into the effect bodies.
inline constexpr uint8_t NUM_GEQ_CHANNELS = 16;

// FX.cpp lines 76 to 84. Nyquist of the 22 kHz sampling the usermod uses.
inline constexpr float MAX_FREQUENCY = 11025.0f;
inline constexpr float MAX_FREQ_LOG10 = 4.04238f;

// WLED's um_soundSimulations_t, selected per effect by the metadata key `si`.
enum SoundSimulation : uint8_t {
  UMS_BEAT_SIN = 0,
  UMS_WE_WILL_ROCK_YOU = 1,
  UMS_10_13 = 2,
  UMS_14_3 = 3,
};

/* One frame of audio analysis. Field order matches WLED's `u_data` indices so the
 * two can be read side by side:
 *
 *   0 volume_smth   1 volume_raw   2 fft_result[16]   3 sample_peak
 *   4 fft_major_peak   5 my_magnitude   6 max_vol   7 bin_num
 *
 * and then the four MoonModules additions at indices 8 to 11.
 *
 * `max_vol` and `bin_num` are written back by effects from their own sliders,
 * which is why `Segment::audio()` hands out a non-const reference. */
struct AudioData {
  // Smoothed volume, 0 to 255 in practice. WLED `volumeSmth`.
  float volume_smth{0.0f};
  // Unsmoothed volume. WLED `volumeRaw`; a few effects read it as int16_t.
  uint16_t volume_raw{0};
  // The 16 GEQ bands, 0 to 255. WLED `fftResult`.
  uint8_t fft_result[NUM_GEQ_CHANNELS]{};
  // Beat flag for this frame. WLED `samplePeak`.
  uint8_t sample_peak{0};
  // Dominant frequency in Hz. WLED `FFT_MajorPeak`.
  float fft_major_peak{1.0f};
  // Magnitude of that peak. WLED `my_magnitude`.
  float my_magnitude{0.0f};
  // Written back by effects from a UI slider. WLED `maxVol`, `binNum`.
  uint8_t max_vol{31};
  uint8_t bin_num{8};

  // MoonModules extras, u_data[8] to u_data[11]. Kept because the MM effects in
  // the fx_mm batch read them and they cost 14 bytes.
  float fft_major_peak_smth{1.0f};
  // Logarithmic microphone level, 0 to 255.
  float sound_pressure{0.0f};
  // Current AGC gain scaled to 0 to 255. Input level is 255 minus this.
  float agc_sensitivity{0.0f};
  uint16_t zero_crossing_count{0};
};

/* A real analysis source. The microphone and FFT implementation is a later phase;
 * this interface is defined now so effect porters can write against the final
 * shape. Nothing in the engine or in an effect ever constructs one. */
class AudioSource {
 public:
  virtual ~AudioSource() = default;
  // The most recent frame of analysis. Called at most once per effect per frame.
  virtual const AudioData &data() const = 0;
  // False while the source is starting up or has no samples yet, which makes the
  // engine fall back to the simulation.
  virtual bool has_data() const = 0;
};

// Attaches or detaches the process-wide source. Passing nullptr goes back to the
// simulation. There is one canvas and one segment, so one source is enough.
void set_audio_source(AudioSource *source);
AudioSource *audio_source();

/* WLED's simulateSound(), which synthesises the whole struct from the frame clock
 * so all 37 audio effects still animate with no microphone attached. Regenerated
 * once per (simulation_id, now) pair, so every effect in a frame sees one
 * coherent set of numbers, as WLED's per-frame `strip.now` intends. */
AudioData &simulate_sound(uint8_t simulation_id, uint32_t now);

// What Segment::audio() returns: the attached source when it has data, the
// simulation otherwise.
AudioData &audio_data(uint8_t simulation_id, uint32_t now);

}  // namespace wled_fx
}  // namespace esphome
