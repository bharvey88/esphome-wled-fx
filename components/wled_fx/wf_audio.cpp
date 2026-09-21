/* See wf_audio.h for the WLED attribution and licence notice. */

// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_audio.h"

#include "wf_math.h"

namespace esphome {
namespace wled_fx {

namespace {

AudioSource *g_source = nullptr;

// The simulation's own state, regenerated when the frame timestamp moves.
AudioData g_sim{};
uint32_t g_sim_now = 0xFFFFFFFFu;
uint8_t g_sim_id = 0xFF;

// 120 bpm, the tempo the simulated spectrum's own beatsin8_t calls are built on.
constexpr uint32_t SIM_BEAT_PERIOD_MS = 60000 / 120;
// Which beat the last generated frame fell in, so a peak fires once per beat.
uint32_t g_sim_beat = 0xFFFFFFFFu;

}  // namespace

void set_audio_source(AudioSource *source) { g_source = source; }

AudioSource *audio_source() { return g_source; }

AudioData &simulate_sound(uint8_t simulation_id, uint32_t now) {
  if (g_sim_now == now && g_sim_id == simulation_id)
    return g_sim;
  g_sim_now = now;
  g_sim_id = simulation_id;

  const uint32_t ms = now;
  uint8_t *fft_result = g_sim.fft_result;
  float &volume_smth = g_sim.volume_smth;

  switch (simulation_id) {
    default:
    case UMS_BEAT_SIN:
      for (int i = 0; i < 16; i++)
        fft_result[i] = beatsin8_t(120 / (i + 1), 0, 255, now);
      volume_smth = fft_result[8];
      break;
    case UMS_WE_WILL_ROCK_YOU:
      if (ms % 2000 < 200) {
        volume_smth = hw_random8();
        for (int i = 0; i < 5; i++)
          fft_result[i] = hw_random8();
      } else if (ms % 2000 < 400) {
        volume_smth = 0;
        for (int i = 0; i < 16; i++)
          fft_result[i] = 0;
      } else if (ms % 2000 < 600) {
        volume_smth = hw_random8();
        for (int i = 5; i < 11; i++)
          fft_result[i] = hw_random8();
      } else if (ms % 2000 < 800) {
        volume_smth = 0;
        for (int i = 0; i < 16; i++)
          fft_result[i] = 0;
      } else if (ms % 2000 < 1000) {
        volume_smth = hw_random8();
        for (int i = 11; i < 16; i++)
          fft_result[i] = hw_random8();
      } else {
        volume_smth = 0;
        for (int i = 0; i < 16; i++)
          fft_result[i] = 0;
      }
      break;
    case UMS_10_13:
      for (int i = 0; i < 16; i++)
        fft_result[i] = perlin8(beatsin8_t(90 / (i + 1), 0, 200, now) * 15 + (ms >> 10), ms >> 3);
      volume_smth = fft_result[8];
      break;
    case UMS_14_3:
      for (int i = 0; i < 16; i++)
        fft_result[i] = perlin8(beatsin8_t(120 / (i + 1), 10, 30, now) * 10 + (ms >> 14), ms >> 3);
      volume_smth = fft_result[8];
      break;
  }

  /* Upstream's draw, kept and thrown away. The value is not used, the draw is:
   * the generator is shared with every effect, so consuming one fewer random
   * number per frame than upstream puts every audio effect on a different
   * random sequence from the same frame on. Round 2 caught it through the Fw
   * Starburst audio black allowance, which had to be widened when the draw
   * disappeared. Round 1's fix replaced the line; this keeps the line and
   * overrides only what it decided. */
  (void) hw_random8();

  /* Upstream decides the peak from that draw: `samplePeak = hw_random8() > 250;`
   * (wled00/util.cpp:662), outside the switch, so it is the same in all four
   * simulation modes and no choice of `si` changes it. Five draws out of 256 is
   * a peak on about 2 percent of frames, and Puddlepeak, which draws nothing
   * except on a peak, is blank with the simulated source as a result. Its rate
   * is also frame rate dependent, about 0.85 a second at a 23 ms frame and 0.39
   * at 50 ms, because it is a coin toss per frame rather than per second.
   *
   * This is the one line of the simulation that is the port's rather than WLED's,
   * and it is deviation 27. The rate is the 120 bpm the simulated spectrum is
   * already built on: every mode's band 0 is `beatsin8_t(120 / (0 + 1), ...)`. One
   * peak per beat of that, counted off the frame timestamp rather than the frame
   * number, so it is the same two peaks a second at any frame rate and is
   * reproducible in the simulator. */
  const uint32_t beat = ms / SIM_BEAT_PERIOD_MS;
  g_sim.sample_peak = (beat != g_sim_beat) ? 1 : 0;
  g_sim_beat = beat;
  // Walks the full 21 Hz to 8200 Hz range.
  g_sim.fft_major_peak = 21 + (volume_smth * volume_smth) / 8.0f;
  /* Upstream's simulateSound() reassigns these two on every call as well
   * (wled00/util.cpp:664-665), so an effect that writes its own values sees them
   * for the rest of the frame and then loses them. Only the real source leaves
   * them alone, which is what PORTING.md section 7 is about. */
  g_sim.max_vol = 31;
  g_sim.bin_num = 8;
  g_sim.volume_raw = static_cast<uint16_t>(volume_smth);
  g_sim.my_magnitude = 10000.0f / 8.0f;
  if (volume_smth < 1)
    g_sim.my_magnitude = 0.001f;  // noise gate closed, mute

  // MoonModules aliases the extras onto the values it already has.
  g_sim.fft_major_peak_smth = g_sim.fft_major_peak;
  g_sim.sound_pressure = volume_smth;
  g_sim.agc_sensitivity = volume_smth;
  g_sim.zero_crossing_count = 0;

  return g_sim;
}

AudioData &audio_data(uint8_t simulation_id, uint32_t now) {
  if (g_source != nullptr && g_source->has_data()) {
    // The source owns its buffer; effects that write max_vol or bin_num back are
    // writing into the source's own copy, which is what WLED does too.
    return const_cast<AudioData &>(g_source->data());
  }
  return simulate_sound(simulation_id, now);
}

}  // namespace wled_fx
}  // namespace esphome
