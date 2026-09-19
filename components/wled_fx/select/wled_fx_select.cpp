#include "wled_fx_select.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wled_fx {

static const char *const TAG = "wled_fx.select";

void WledFxSelect::setup() {
  FixedVector<const char *> options;
  if (this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT) {
    const size_t total = EffectRegistry::count();
    // Effect names are a prefix of the metadata string, so they have to be copied
    // out. One arena allocation at setup holds them all for the life of the device.
    size_t arena_size = 0;
    for (size_t i = 0; i < total; i++) {
      char buffer[64];
      arena_size += effect_name(*EffectRegistry::at(i), buffer, sizeof(buffer)) + 1;
    }
    RAMAllocator<char> allocator;
    this->name_arena_ = allocator.allocate(arena_size);
    if (this->name_arena_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the effect name list");
      this->mark_failed();
      return;
    }
    options.init(total);
    char *cursor = this->name_arena_;
    for (size_t i = 0; i < total; i++) {
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

  const std::string current = this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT
                                  ? this->parent_->current_effect_name()
                                  : this->parent_->current_palette_name();
  if (!current.empty())
    this->publish_state(current);
}

void WledFxSelect::control(const std::string &value) {
  const bool ok = this->type_ == WledFxSelectType::WLED_FX_SELECT_TYPE_EFFECT
                      ? this->parent_->set_effect_by_name(value)
                      : this->parent_->set_palette_by_name(value);
  if (!ok) {
    ESP_LOGW(TAG, "'%s' is not available in this build", value.c_str());
    return;
  }
  this->publish_state(value);
}

void WledFxSelect::dump_config() { LOG_SELECT("", "WLED FX Select", this); }

}  // namespace wled_fx
}  // namespace esphome
