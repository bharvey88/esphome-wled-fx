// Built at -O2 when `optimize: speed` is set. Must come first; see wf_optimize.h.
#include "../wf_optimize.h"

#include "wled_fx_select.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.select";

bool WledFxSelect::in_scope_(size_t index) const {
  /* Offered first, always: `scope` narrows what this dropdown shows, it never
   * widens it past what the output can run. */
  if (!this->parent_->effect_offered(index))
    return false;
  const EffectInfo *info = EffectRegistry::at(index);
  if (info == nullptr)
    return false;
  switch (this->scope_) {
    case WledFxSelectScope::WLED_FX_SELECT_SCOPE_PANEL:
      return effect_available(*info, true, false);
    case WledFxSelectScope::WLED_FX_SELECT_SCOPE_STRIP:
      return effect_available(*info, false, false);
    default:
      return true;
  }
}

void WledFxSelect::setup() {
  FixedVector<const char *> options;
  if (this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT) {
    const size_t total = EffectRegistry::count();
    /* Only the effects this output can actually run, narrowed to this select's
     * scope. A 1D-only effect on a matrix, or a 2D-only one on a strip, is not
     * something the user can pick their way into: the rule is in
     * effect_available(), and the same rule rejects it at config time and
     * ignores it at runtime. */
    size_t offered = 0;
    // Effect names are a prefix of the metadata string, so they have to be copied
    // out. One arena allocation at setup holds them all for the life of the device.
    size_t arena_size = 0;
    for (size_t i = 0; i < total; i++) {
      if (!this->in_scope_(i))
        continue;
      offered++;
      char buffer[64];
      arena_size += effect_name(*EffectRegistry::at(i), buffer, sizeof(buffer)) + 1;
    }
    if (offered == 0) {
      /* Config validation refuses an output that can run nothing at all, so
       * with the default scope this means the two rules disagree. With a
       * narrowed scope it is an ordinary mistake: `scope: strip` on a build
       * that did not opt in to the 1D effects, say. Both deserve the same
       * treatment, which is to fail loudly rather than publish an empty
       * dropdown nobody can use. */
      ESP_LOGE(TAG, "No compiled-in effect fits this select, so there is nothing to offer");
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
      if (!this->in_scope_(i))
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

  if (this->restore_) {
    this->pref_ = this->make_entity_preference<uint8_t>();
    uint8_t stored;
    // A build whose option list shrank would otherwise restore somebody else's
    // palette, so an index that no longer fits is dropped.
    if (this->pref_.load(&stored) && stored < this->traits.get_options().size())
      this->control(this->traits.get_options()[stored]);
  }

  this->publish_current_();
  // An action or another entity can change the effect, so follow the engine
  // rather than only publishing once here.
  this->parent_->add_on_state_change_callback([this]() { this->publish_current_(); });
}

void WledFxSelect::publish_current_() {
  if (this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT &&
      !this->in_scope_(this->parent_->engine().effect_index())) {
    /* Something outside this dropdown's scope is running, which on a panel
     * with two effect selects is the normal state of one of them. Nothing to
     * publish: the option is not on the list, and the entity keeps the last
     * one that was. What is actually running is on the other select, and on
     * the "Effect name" sensor the hardware-test harness adds. */
    return;
  }
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
  if (this->restore_) {
    // The engine's spelling, not the one that arrived, and as a position in
    // the option list so one byte covers it.
    const StringRef option = this->current_option();
    const auto index = this->index_of(option.c_str(), option.size());
    if (index.has_value()) {
      const uint8_t stored = static_cast<uint8_t>(*index);
      this->pref_.save(&stored);
    }
  }
}

void WledFxSelect::dump_config() { LOG_SELECT("", "WLED FX Select", this); }

}  // namespace wled_fx
}  // namespace esphome
