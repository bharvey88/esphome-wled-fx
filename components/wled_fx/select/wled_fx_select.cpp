#include "wled_fx_select.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.select";

void WledFxSelect::setup() {
  FixedVector<const char *> options;
  if (this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT) {
    const size_t total = EffectRegistry::count();
    /* Only the effects this output can actually run. A 1D-only effect on a
     * matrix, or a 2D-only one on a strip, is not something the user can pick
     * their way into: the rule is in effect_available(), and the same rule
     * rejects it at config time and ignores it at runtime. */
    size_t offered = 0;
    // Effect names are a prefix of the metadata string, so they have to be copied
    // out. One arena allocation at setup holds them all for the life of the device.
    size_t arena_size = 0;
    for (size_t i = 0; i < total; i++) {
      if (!this->parent_->effect_offered(i))
        continue;
      offered++;
      char buffer[64];
      arena_size += effect_name(*EffectRegistry::at(i), buffer, sizeof(buffer)) + 1;
    }
    if (offered == 0) {
      // Config validation refuses this, so it means the two rules disagree.
      ESP_LOGE(TAG, "No compiled-in effect runs on this output, so there is nothing to select");
      this->mark_failed();
      return;
    }
    RAMAllocator<char> allocator;
    this->name_arena_ = allocator.allocate(arena_size);
    if (this->name_arena_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the effect name list");
      this->mark_failed();
      return;
    }
    options.init(offered);
    char *cursor = this->name_arena_;
    for (size_t i = 0; i < total; i++) {
      if (!this->parent_->effect_offered(i))
        continue;
      const size_t len = effect_name(*EffectRegistry::at(i), cursor, 64);
      options.push_back(cursor);
      cursor += len + 1;
    }
  } else {
    const size_t total = palette_count();
    options.init(total);
    for (size_t i = 0; i < total; i++)
      options.push_back(PALETTE_NAMES[i]);
  }
  this->traits.set_options(options);

  this->publish_current_();
  // An action or another entity can change the effect, so follow the engine
  // rather than only publishing once here.
  this->parent_->add_on_state_change_callback([this]() { this->publish_current_(); });
}

void WledFxSelect::publish_current_() {
  const std::string current = this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT
                                  ? this->parent_->current_effect_name()
                                  : this->parent_->current_palette_name();
  if (!current.empty() && (!this->has_state() || this->current_option() != current))
    this->publish_state(current);
}

void WledFxSelect::control(const std::string &value) {
  const bool ok = this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT
                      ? this->parent_->set_effect_by_name(value)
                      : this->parent_->set_palette_by_name(value);
  if (!ok) {
    ESP_LOGW(TAG, "'%s' is not available in this build", value.c_str());
    // Put the dropdown back to what the engine is actually running.
    this->publish_current_();
    return;
  }
  // A successful change already published through the state change callback,
  // and it published the registry's spelling rather than whatever arrived.
}

void WledFxSelect::dump_config() { LOG_SELECT("", "WLED FX Select", this); }

}  // namespace wled_fx
}  // namespace esphome
