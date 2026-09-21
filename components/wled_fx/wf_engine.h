#pragma once

/* Ties the canvas, the segment, the registry and the shared random palette
 * together. Both front ends and the host simulator drive the engine through this
 * one class; nothing else needs to know how an effect is invoked. */

#include <cstdint>

#include "wf_canvas.h"
#include "wf_registry.h"
#include "wf_segment.h"

namespace esphome {
namespace wled_fx {

// Which segment fields the user pinned in YAML or at runtime. Anything not pinned
// is refilled from the effect's own metadata defaults when the effect changes.
enum ControlOverride : uint16_t {
  OVERRIDE_SPEED = 1 << 0,
  OVERRIDE_INTENSITY = 1 << 1,
  OVERRIDE_CUSTOM1 = 1 << 2,
  OVERRIDE_CUSTOM2 = 1 << 3,
  OVERRIDE_CUSTOM3 = 1 << 4,
  OVERRIDE_CHECK1 = 1 << 5,
  OVERRIDE_CHECK2 = 1 << 6,
  OVERRIDE_CHECK3 = 1 << 7,
  OVERRIDE_PALETTE = 1 << 8,
};

class Engine {
 public:
  // Allocates the canvas. Call once from setup(). Returns false on failure.
  bool init(uint16_t width, uint16_t height);

  Canvas &canvas() { return this->canvas_; }
  Segment &segment() { return this->seg_; }

  const EffectInfo *effect() const { return this->effect_; }
  // Selects by display name. Returns false and leaves the effect alone when the
  // name is not registered (or was not compiled in).
  bool set_effect(const char *name);
  bool set_effect_index(size_t index);
  size_t effect_index() const { return this->effect_index_; }

  /* Whether a control set from here on sticks across an effect change.
   *
   * True, the default, is WLED's own behaviour and what a control pinned in
   * YAML needs: the value survives every later effect. False makes a control
   * belong to the effect it was set on, so the next effect change refills it
   * from that effect's own metadata defaults. Turning it off does not unpin
   * what is already pinned; clear_override() does that.
   *
   * Nothing in the component changes this by itself. It is here so a front end
   * can offer the choice, because a slider that silently follows you into the
   * next effect leaves that effect in a state its author never meant. */
  void set_sticky_controls(bool sticky) { this->sticky_ = sticky; }

  /* The output's frame period, which "* Random Cycle" needs: upstream sizes
   * its palette blend from the frame time so the new palette lands inside the
   * transition time whatever rate the output runs at. WLED's own FRAMETIME
   * until a front end says otherwise. */
  void set_frame_time(uint16_t ms) { this->frame_time_ = ms > 0 ? ms : 1; }
  bool sticky_controls() const { return this->sticky_; }

  // Setting a control applies it now, and keeps it across effect changes while
  // sticky_controls() is true.
  void set_speed(uint8_t v);
  void set_intensity(uint8_t v);
  void set_custom1(uint8_t v);
  void set_custom2(uint8_t v);
  void set_custom3(uint8_t v);
  void set_check1(bool v);
  void set_check2(bool v);
  void set_check3(bool v);
  void set_palette(uint8_t v);
  bool set_palette_by_name(const char *name);
  void clear_override(uint16_t flag) { this->overrides_ &= ~flag; }

  void set_primary_color(uint32_t c) { this->seg_.colors[0] = c; }
  void set_secondary_color(uint32_t c) { this->seg_.colors[1] = c; }
  void set_tertiary_color(uint32_t c) { this->seg_.colors[2] = c; }
  void set_text(const char *text) { this->seg_.text = text != nullptr ? text : ""; }

  /* Puts the current effect back to frame zero, the way selecting it again
   * would: the scratch block is kept but marked stale, so the effect reinitialises
   * into it. The light front end calls this when the effect is stopped, so turning
   * the light on starts the animation rather than resuming it mid stride. */
  void restart_effect() { this->seg_.reset(); }

  // Runs one frame at timestamp now (milliseconds).
  void render(uint32_t now);

 protected:
  void apply_effect_defaults_();
  // Records or clears the override for one control, according to sticky_.
  void note_override_(uint16_t flag) {
    if (this->sticky_)
      this->overrides_ |= flag;
    else
      this->overrides_ &= ~flag;
  }

  Canvas canvas_;
  Segment seg_;
  RandomPalette random_palette_;
  const EffectInfo *effect_{nullptr};
  size_t effect_index_{0};
  uint16_t overrides_{0};
  uint16_t frame_time_{23};
  bool sticky_{true};
};

}  // namespace wled_fx
}  // namespace esphome
