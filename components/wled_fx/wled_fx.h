#pragma once

#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "wf_engine.h"

#ifdef USE_DISPLAY
#include "esphome/components/display/display.h"
#endif

namespace esphome {
namespace wled_fx {

/* Shared runtime surface. Both front ends own an Engine and expose the same
 * controls, so the actions and the select / number / switch platforms work
 * against either one. */
class WledFxController {
 public:
  Engine &engine() { return this->engine_; }

  /* --- the shape of this output ----------------------------------------------
   *
   * Set by codegen, because the shape is fixed by the configuration: a display
   * front end is a matrix, and a light effect is one only when it was given a
   * width and a height. Which effects the output offers follows from it; see
   * effect_available() in wf_registry.h for the rule and why it exists.
   *
   * Both are set before any component's setup() runs, so an entity that builds
   * a list of effects in its own setup() already sees the right answer. */
  void set_layout_2d(bool two_dimensional) { this->layout_2d_ = two_dimensional; }
  bool layout_2d() const { return this->layout_2d_; }
  void set_include_1d_effects(bool include) { this->include_1d_effects_ = include; }
  bool include_1d_effects() const { return this->include_1d_effects_; }

  // True when this output offers the registered effect at `index`.
  bool effect_offered(size_t index) const;
  // The registered index of the first effect this output offers, or SIZE_MAX.
  size_t first_offered_effect() const;
  /* Moves off an effect this output does not offer. The engine starts on the
   * first registered effect, which on a matrix in a build that also carries a
   * strip is usually a 1D one, so a configuration that named no effect would
   * otherwise boot on something it is not allowed to select. Called by both
   * front ends once their canvas exists. */
  void ensure_offered_effect();

  // Returns false when the effect or palette name is not compiled in, or when
  // this output does not offer it.
  bool set_effect_by_name(const std::string &name);
  bool set_palette_by_name(const std::string &name);
  // Steps to the next effect this output offers, wrapping round.
  void next_effect();

  std::string current_effect_name() const;
  std::string current_palette_name() const;

  void set_text_value(const std::string &text);

  /* The three WLED colour slots. Which of them an effect uses, and what it
   * calls them, is in its metadata: see format_effect_colors(). The light
   * platform and the wled_fx.set_color action both come through here. */
  void set_color_slot(unsigned slot, uint32_t rgb);
  uint32_t color_slot(unsigned slot) const;

  /* Master output, which is what WLED's brightness slider and power button do:
   * the effect keeps running and the frame is scaled on its way out, and with
   * the output off the panel is blanked once and then left alone. Both front
   * ends honour this. */
  void set_output_brightness(uint8_t brightness) { this->output_brightness_ = brightness; }
  uint8_t output_brightness() const { return this->output_brightness_; }
  void set_output_enabled(bool enabled) { this->output_enabled_ = enabled; }
  bool output_enabled() const { return this->output_enabled_; }

  /* --- per-effect profiling --------------------------------------------------
   *
   * Microseconds in the effect function and microseconds pushing the frame out,
   * as min, max and a running total, for as long as the current effect has been
   * running. Eight counters and two micros() calls a frame, so it is always
   * compiled in rather than hidden behind an option: at 43 fps that is under a
   * thousandth of the frame budget and it is the only way to answer "which
   * effects are slow on this board" without reflashing.
   *
   * The first second of an effect is left out. Allocating the particle system
   * or the first pass of a noise field is real work, but it is start-up cost,
   * not the steady state the frame rate depends on. */
  struct ProfileStats {
    uint32_t frames{0};
    uint32_t window_ms{0};  // over which those frames were counted
    uint32_t render_min_us{0};
    uint32_t render_max_us{0};
    uint64_t render_total_us{0};
    uint32_t output_min_us{0};
    uint32_t output_max_us{0};
    uint64_t output_total_us{0};

    uint32_t render_avg_us() const {
      return this->frames == 0 ? 0 : static_cast<uint32_t>(this->render_total_us / this->frames);
    }
    uint32_t output_avg_us() const {
      return this->frames == 0 ? 0 : static_cast<uint32_t>(this->output_total_us / this->frames);
    }
    // Frames per second over the measured window, times ten so it stays integer.
    uint32_t fps_x10() const {
      return this->window_ms == 0 ? 0 : static_cast<uint32_t>(this->frames * 10000ull / this->window_ms);
    }
  };

  const ProfileStats &profile() const { return this->profile_; }
  // Starts a fresh window, which an effect change does on its own.
  void reset_profile(uint32_t now);

  /* The frame clock. ESPHome's main loop ticks about every 16 ms and its
   * scheduler re-arms an interval from the moment the callback ran, so a 23 ms
   * interval becomes 32 ms in practice, and every effect runs at 31 fps instead
   * of the 42 fps WLED's FRAMETIME means. Speeds are visibly wrong.
   *
   * Accumulating the deadline instead of re-arming it fixes that: the gate
   * alternates between one tick and two, and the average comes out at the
   * configured period. If the loop stalls long enough that the deadline is more
   * than one period in the past, it resynchronises rather than trying to catch
   * up with a burst of frames nobody would see.
   *
   * Both front ends use this, so the light effect and the display front end run
   * at the same rate from the same code. */
  void set_frame_interval(uint32_t interval_ms) {
    this->frame_interval_ = interval_ms;
    // The engine needs it too: "* Random Cycle" sizes its palette blend from
    // the frame period. WLED reads the same number out of the strip.
    this->engine_.set_frame_time(static_cast<uint16_t>(interval_ms > 0xFFFF ? 0xFFFF : interval_ms));
  }
  uint32_t frame_interval() const { return this->frame_interval_; }

  // Restarts the clock so the next call to due_() renders immediately.
  void reset_frame_clock() { this->have_deadline_ = false; }

  /* Changing the effect refills every control the user did not pin from the new
   * effect's own metadata defaults, so a `select`, `number` or `switch` entity
   * that published its value at setup is stale the moment anything else moves.
   * Those entities subscribe here and republish. The callbacks run on the main
   * loop, from whatever changed the state, never per frame. */
  void add_on_state_change_callback(std::function<void()> &&callback) {
    this->state_change_callbacks_.push_back(std::move(callback));
  }
  // Called by whatever touched the engine outside this class, the actions.
  void notify_state_change() {
    for (auto &callback : this->state_change_callbacks_)
      callback();
  }

 protected:
  // True when `now` has reached the next frame deadline, which it then advances.
  bool frame_due_(uint32_t now);

  /* Called once per rendered frame by whichever front end is driving. Restarts
   * the window when the effect changed, and folds the two timings in once the
   * effect has been running for a second. */
  void profile_frame_(uint32_t now, uint32_t render_us, uint32_t output_us);

  // The master brightness applied to one channel. 255 is a no-op and is skipped.
  uint8_t scale_output_(uint8_t value) const {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * (this->output_brightness_ + 1)) >> 8);
  }

  Engine engine_;
  std::string text_;
  std::vector<std::function<void()>> state_change_callbacks_;
  uint32_t frame_interval_{FRAMETIME};
  uint32_t next_frame_{0};
  bool have_deadline_{false};
  uint8_t output_brightness_{255};
  bool output_enabled_{true};
  bool layout_2d_{false};
  bool include_1d_effects_{false};

  ProfileStats profile_;
  uint32_t profile_effect_start_{0};
  size_t profile_effect_index_{SIZE_MAX};
  bool profile_counting_{false};
};

#ifdef USE_DISPLAY
/* Display front end. Owns the frame clock, renders into the canvas and pushes the
 * whole frame with a single draw_pixels_at() call.
 *
 * hub75 only flips its double buffer inside update(), and update() is never called
 * when the display has `update_interval: never`, so this component calls update()
 * itself right after the blit. With `auto_clear_enabled: false` and no lambda or
 * pages on the display, that update() is just the buffer flip. Both of those
 * display settings are enforced at codegen time. */
class WledFxDisplay : public Component, public WledFxController {
 public:
  void set_display(display::Display *display) { this->display_ = display; }
  void set_dimensions(int width, int height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_gamma(float gamma) { this->gamma_ = gamma; }

  /* How often ESPHome should run its component phase at all, in milliseconds,
   * or 0 to leave it alone. See apply_loop_interval_() for what this is for. */
  void set_loop_interval(uint32_t interval_ms) { this->loop_interval_ = interval_ms; }

  void setup() override;
  /* A plain loop(), not a PollingComponent's update(). ESPHome's scheduler
   * re-arms an interval from the moment the callback ran, which on a 16 ms main
   * loop turns a 23 ms interval into 32 ms and every effect into a 31 fps
   * version of itself. The gate in WledFxController accumulates instead, so the
   * average rate is the one that was asked for. */
  void loop() override;
  void dump_config() override;
  // Runs after the display driver, which needs to be up before get_width() works.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  // Blits the frame buffer and flips the display's own buffer.
  void push_frame_();
  /* Folds the master brightness into the gamma table, so the per-pixel loop is
   * three lookups and nothing else whatever the brightness is. Called from
   * loop() when the brightness has moved, which is a slider, not a frame. */
  void rebuild_output_lut_();

  /* --- the main loop, and why the frame gate is not enough ------------------
   *
   * The frame gate renders when the deadline has passed, but it can only be
   * asked once a loop tick, and ESPHome runs its component phase at most every
   * `loop_interval_`, 16 ms by default (esphome/core/application.h). A 23 ms
   * deadline against a 16 ms tick is meant to alternate one tick and two, an
   * average of 23 ms, and for a cheap effect it does: Solid measured 43.1 fps
   * on the M-1.
   *
   * It stops working as soon as a rendered tick runs long. ESPHome times the
   * next tick from the start of the last one, so a tick that took 19 ms is
   * followed by the next at 16 ms after it began, which is 3 ms of waiting, and
   * then the one after that is a further 16 ms away. The 23 ms deadline lands
   * in that gap and the frame is 35 ms late instead of 23. That is the shape of
   * the measured median: 34.6 fps, about 29 ms, on effects whose own work is
   * nowhere near 29 ms.
   *
   * Asking for a shorter tick fixes the quantisation rather than the work: with
   * a tick well inside the frame period the gate lands within one tick of every
   * deadline and the long run average is the period that was configured. The
   * frame gate still accumulates, so nothing renders faster than it was asked
   * to; the shorter tick only stops it rendering slower.
   *
   * A third of the frame period, floored at 4 ms, and never longer than what
   * the application already has, so this can lower the interval and never raise
   * it: whatever else is in the firmware keeps at least the responsiveness it
   * had. The cost is that every component's loop() is polled more often, which
   * on a mains powered panel is idle time being spent. `loop_interval: never`
   * turns it off. */
  void apply_loop_interval_();

  display::Display *display_{nullptr};
  uint8_t *frame_{nullptr};
  int width_{0};
  int height_{0};
  /* WLED's show() stage, on by default there and on by default here. See the
   * `gamma_correct` option in README.md: a display that applies a curve of its
   * own wants this at 1.0 instead, and the hub75 driver is exactly that case
   * until its own `gamma_correct` is set to LINEAR. */
  float gamma_{2.2f};
  // Gamma alone, kept so the combined table can be rebuilt without pow().
  uint8_t gamma_lut_[256]{};
  // Gamma then the master brightness, which is what the frame is built from.
  uint8_t output_lut_[256]{};
  // The brightness output_lut_ was built for. 256 is "never built".
  uint16_t output_lut_brightness_{256};
  // True when the frame buffer landed in internal RAM. dump_config prints it.
  bool frame_internal_{false};
  // The main loop interval to ask for, or 0 for "leave it alone".
  uint32_t loop_interval_{0};
  // True once the all-black frame that the output being off asks for has been
  // pushed, so a blanked panel costs nothing per frame.
  bool blanked_{false};
};
#endif  // USE_DISPLAY

}  // namespace wled_fx
}  // namespace esphome
