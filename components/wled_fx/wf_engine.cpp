// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "wf_optimize.h"

#include "wf_engine.h"

namespace esphome {
namespace wled_fx {

bool Engine::init(uint16_t width, uint16_t height) {
  if (!this->canvas_.allocate(width, height))
    return false;
  if (!this->seg_.set_canvas(&this->canvas_))
    return false;
  if (this->effect_ == nullptr && EffectRegistry::count() > 0)
    this->set_effect_index(0);
  return true;
}

void Engine::apply_effect_defaults_() {
  if (this->effect_ == nullptr)
    return;
  const EffectDefaults d = effect_defaults(*this->effect_);
  if (!(this->overrides_ & OVERRIDE_SPEED))
    this->seg_.speed = d.speed;
  if (!(this->overrides_ & OVERRIDE_INTENSITY))
    this->seg_.intensity = d.intensity;
  if (!(this->overrides_ & OVERRIDE_CUSTOM1))
    this->seg_.custom1 = d.custom1;
  if (!(this->overrides_ & OVERRIDE_CUSTOM2))
    this->seg_.custom2 = d.custom2;
  if (!(this->overrides_ & OVERRIDE_CUSTOM3))
    this->seg_.custom3 = d.custom3;
  if (!(this->overrides_ & OVERRIDE_CHECK1))
    this->seg_.check1 = d.check1;
  if (!(this->overrides_ & OVERRIDE_CHECK2))
    this->seg_.check2 = d.check2;
  if (!(this->overrides_ & OVERRIDE_CHECK3))
    this->seg_.check3 = d.check3;
  /* The palette is upstream's odd one out (wled00/FX_fcn.cpp:616-619). The
   * metadata is read for `pal=` on every effect change, but the segment's
   * palette is only written when the metadata names one: an effect that
   * declares no palette leaves the palette the user is on alone. 112 of the
   * 223 effects here declare none, so this is most of them.
   *
   * `default_palette` is set either way, and it is what a palette of 0,
   * "Default" in the select, renders as while this effect runs. */
  if (d.palette >= 0 && !(this->overrides_ & OVERRIDE_PALETTE))
    this->seg_.palette = static_cast<uint8_t>(d.palette);
  this->seg_.default_palette = d.default_palette;
  // Upstream resets the mapping mode when the metadata does not name one, and
  // leaves the sound simulation alone.
  this->seg_.map1d2d = d.map1d2d >= 0 ? static_cast<uint8_t>(d.map1d2d) : M12_PIXELS;
  if (d.sound_sim >= 0)
    this->seg_.sound_sim = static_cast<uint8_t>(d.sound_sim);
}

bool Engine::set_effect(const char *name) {
  const int index = EffectRegistry::index_of(name);
  if (index < 0)
    return false;
  return this->set_effect_index(static_cast<size_t>(index));
}

bool Engine::set_effect_index(size_t index) {
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return false;
  this->effect_ = info;
  this->effect_index_ = index;
  this->seg_.reset();
  this->apply_effect_defaults_();
  return true;
}

void Engine::set_speed(uint8_t v) {
  this->seg_.speed = v;
  this->note_override_(OVERRIDE_SPEED);
}
void Engine::set_intensity(uint8_t v) {
  this->seg_.intensity = v;
  this->note_override_(OVERRIDE_INTENSITY);
}
void Engine::set_custom1(uint8_t v) {
  this->seg_.custom1 = v;
  this->note_override_(OVERRIDE_CUSTOM1);
}
void Engine::set_custom2(uint8_t v) {
  this->seg_.custom2 = v;
  this->note_override_(OVERRIDE_CUSTOM2);
}
void Engine::set_custom3(uint8_t v) {
  this->seg_.custom3 = v > 31 ? 31 : v;
  this->note_override_(OVERRIDE_CUSTOM3);
}
void Engine::set_check1(bool v) {
  this->seg_.check1 = v;
  this->note_override_(OVERRIDE_CHECK1);
}
void Engine::set_check2(bool v) {
  this->seg_.check2 = v;
  this->note_override_(OVERRIDE_CHECK2);
}
void Engine::set_check3(bool v) {
  this->seg_.check3 = v;
  this->note_override_(OVERRIDE_CHECK3);
}
void Engine::set_palette(uint8_t v) {
  this->seg_.palette = v;
  this->note_override_(OVERRIDE_PALETTE);
}

bool Engine::set_palette_by_name(const char *name) {
  const int id = palette_id_by_name(name);
  if (id < 0)
    return false;
  this->set_palette(static_cast<uint8_t>(id));
  return true;
}

void Engine::render(uint32_t now) {
  if (this->effect_ == nullptr || !this->canvas_.is_allocated())
    return;
  this->random_palette_.step(now, this->frame_time_);
  this->seg_.now = now;
  this->seg_.now_us = now * 1000u;
  this->seg_.begin_draw(this->random_palette_.current());
  this->effect_->fn(this->seg_);
  this->seg_.call++;
}

}  // namespace wled_fx
}  // namespace esphome
