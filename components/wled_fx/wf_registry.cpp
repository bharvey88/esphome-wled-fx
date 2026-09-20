/* See wf_registry.h for the WLED attribution and licence notice. */

#include "wf_registry.h"

#include <cstdlib>
#include <cstring>

namespace esphome {
namespace wled_fx {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Returns a pointer to the start of metadata group `index` (0 based, groups are
// separated by ';'), or nullptr when there are fewer groups than that.
const char *group_at(const char *metadata, int index) {
  const char *p = metadata;
  int current = 0;
  while (current < index) {
    p = strchr(p, ';');
    if (p == nullptr)
      return nullptr;
    p++;
    current++;
  }
  return p;
}

}  // namespace

/* --- metadata strings that do not say what they mean -------------------------
 *
 * The dimensionality group is the fourth ';' group. A few upstream strings have
 * fewer groups than that, so whatever landed in the fourth position gets read as
 * the dimensionality instead. WLED's own web UI reads the same group the same
 * way and has the same problem, so the flags an effect ends up with upstream can
 * be an accident rather than a decision.
 *
 * Editing the string is not the fix: it is copied verbatim from WLED, and the
 * next port of an upstream revision would quietly undo the edit. This table sits
 * beside it instead, so the string stays as upstream wrote it and the flags say
 * what the effect does.
 *
 * It lives in C++ because the metadata it corrects lives in C++. The firmware
 * reads it here and the config validation reads this same table out of this
 * file, so there is one list and the two cannot drift; tools/check_effect_names.py
 * fails if they ever do.
 *
 * Names are matched the way every other name is, case insensitively against the
 * display name. */
struct FlagOverride {
  const char *name;
  uint8_t flags;
};

const FlagOverride FLAG_OVERRIDES[] = {
    /* "Flow Stripe@Hue speed,Effect speed;;!;pal=11" has only four groups, and
     * the fourth is its defaults rather than its dimensionality. The parser
     * reads "pal=11", finds the '1' in the value, and calls the effect 1D. That
     * answer is right and the reasoning is not: a default of pal=12 on a string
     * of the same shape would have made the same effect 2D. mode_FlowStripe()
     * walks the strip with SEGLEN and has no 2D branch at all, so pin it. */
    {"Flow Stripe", EFFECT_FLAG_1D},
};
constexpr size_t FLAG_OVERRIDE_COUNT = sizeof(FLAG_OVERRIDES) / sizeof(FLAG_OVERRIDES[0]);

uint8_t effect_flags(const EffectInfo &info) { return effect_defaults(info).flags; }

bool effect_runs_1d(const EffectInfo &info) {
  return (effect_flags(info) & (EFFECT_FLAG_1D | EFFECT_FLAG_0D)) != 0;
}

bool effect_runs_2d(const EffectInfo &info) { return (effect_flags(info) & EFFECT_FLAG_2D) != 0; }

bool effect_available(const EffectInfo &info, bool two_dimensional, bool include_1d) {
  if (!two_dimensional)
    return effect_runs_1d(info);
  return effect_runs_2d(info) || (include_1d && effect_runs_1d(info));
}

size_t effect_name(const EffectInfo &info, char *dest, size_t dest_size) {
  if (dest_size == 0)
    return 0;
  size_t j = 0;
  const char *src = info.metadata;
  while (j + 1 < dest_size && src[j] != '\0' && src[j] != '@') {
    dest[j] = src[j];
    j++;
  }
  dest[j] = '\0';
  return j;
}

bool effect_name_equals(const EffectInfo &info, const char *name) {
  if (name == nullptr)
    return false;
  const char *a = info.metadata;
  const char *b = name;
  while (*a != '\0' && *a != '@' && *b != '\0') {
    if (lower(*a) != lower(*b))
      return false;
    a++;
    b++;
  }
  return (*a == '\0' || *a == '@') && *b == '\0';
}

EffectDefaults effect_defaults(const EffectInfo &info) {
  EffectDefaults out;

  // Group 3 is the palette section: a leading digit is the default palette ID.
  const char *palette_group = group_at(info.metadata, 2);
  if (palette_group != nullptr && *palette_group >= '0' && *palette_group <= '9')
    out.palette = static_cast<uint8_t>(strtol(palette_group, nullptr, 10));

  // Group 4 is the dimensionality and audio flag set.
  const char *flag_group = group_at(info.metadata, 3);
  uint8_t flags = 0;
  if (flag_group != nullptr) {
    for (const char *p = flag_group; *p != '\0' && *p != ';'; p++) {
      switch (*p) {
        case '0':
          flags |= EFFECT_FLAG_0D;
          break;
        case '1':
          flags |= EFFECT_FLAG_1D;
          break;
        case '2':
          flags |= EFFECT_FLAG_2D;
          break;
        case 'v':
          flags |= EFFECT_FLAG_VOLUME;
          break;
        case 'f':
          flags |= EFFECT_FLAG_FFT;
          break;
        default:
          break;
      }
    }
  }
  out.flags = flags == 0 ? static_cast<uint8_t>(EFFECT_FLAG_1D) : flags;
  // An effect whose metadata has no dimensionality group to read gets its flags
  // from the table above instead. See the comment there.
  for (size_t i = 0; i < FLAG_OVERRIDE_COUNT; i++) {
    if (effect_name_equals(info, FLAG_OVERRIDES[i].name)) {
      out.flags = FLAG_OVERRIDES[i].flags;
      break;
    }
  }

  // Defaults live in the LAST ';' group, which is how WLED reads them too. Keys
  // are sx, ix, c1, c2, c3, o1, o2, o3, pal, m12 and si.
  const char *last = strrchr(info.metadata, ';');
  if (last == nullptr)
    return out;
  last++;

  struct KeyTarget {
    const char *key;
    uint8_t *target;
  };
  const KeyTarget numeric[] = {
      {"sx", &out.speed},   {"ix", &out.intensity}, {"c1", &out.custom1},  {"c2", &out.custom2},
      {"c3", &out.custom3}, {"pal", &out.palette},  {"m12", &out.map1d2d}, {"si", &out.sound_sim},
  };
  bool *const checks[] = {&out.check1, &out.check2, &out.check3};
  const char *const check_keys[] = {"o1", "o2", "o3"};

  const char *p = last;
  while (*p != '\0') {
    // Split on commas. Each item is key=value.
    const char *eq = strchr(p, '=');
    const char *comma = strchr(p, ',');
    if (eq == nullptr || (comma != nullptr && eq > comma)) {
      if (comma == nullptr)
        break;
      p = comma + 1;
      continue;
    }
    const size_t key_len = static_cast<size_t>(eq - p);
    const long value = strtol(eq + 1, nullptr, 10);
    for (const auto &kt : numeric) {
      if (strlen(kt.key) == key_len && strncmp(p, kt.key, key_len) == 0) {
        *kt.target = static_cast<uint8_t>(value);
        break;
      }
    }
    for (size_t i = 0; i < 3; i++) {
      if (strlen(check_keys[i]) == key_len && strncmp(p, check_keys[i], key_len) == 0)
        *checks[i] = value != 0;
    }
    if (comma == nullptr)
      break;
    p = comma + 1;
  }

  return out;
}

// --- control labels ----------------------------------------------------------

const char *const SLIDER_LABEL_DEFAULTS[5] = {"Speed", "Intensity", "Custom 1", "Custom 2", "Custom 3"};
const char *const CHECK_LABEL_DEFAULTS[3] = {"Check 1", "Check 2", "Check 3"};
// WLED's three colour slot buttons: effect colour, background, custom.
const char *const COLOR_LABEL_DEFAULTS[3] = {"Fx", "Bg", "Cs"};
const char *const PALETTE_LABEL_DEFAULT = "Color palette";

namespace {

/* Splits one metadata group on commas into at most `max_items` labels. An item
 * that is empty means the effect does not use that control, which is how WLED
 * hides it; "!" means WLED's own name for it. */
void split_labels(const char *group, ControlLabel *out, unsigned max_items) {
  if (group == nullptr)
    return;
  unsigned item = 0;
  const char *start = group;
  for (const char *p = group;; p++) {
    if (*p != '\0' && *p != ';' && *p != ',')
      continue;
    if (item < max_items) {
      size_t len = static_cast<size_t>(p - start);
      // An embedded default, "Label=7", carries the value for the UI only.
      for (size_t i = 0; i < len; i++) {
        if (start[i] == '=') {
          len = i;
          break;
        }
      }
      if (len > 0) {
        out[item].text = start;
        out[item].length = static_cast<uint8_t>(len > 255 ? 255 : len);
        out[item].is_default = len == 1 && start[0] == '!';
      }
    }
    item++;
    if (*p == '\0' || *p == ';')
      break;
    start = p + 1;
  }
}

// True when the whole label is digits, which for the palette group means WLED
// pins the palette and hides the selector.
bool all_digits(const ControlLabel &label) {
  if (!label.used())
    return false;
  for (uint8_t i = 0; i < label.length; i++) {
    if (label.text[i] < '0' || label.text[i] > '9')
      return false;
  }
  return true;
}

/* Appends at most what fits, all of it or none of it, so a truncated line never
 * ends half way through the multi-byte separator. Returns the new length. */
size_t append(char *dest, size_t len, size_t cap, const char *src, size_t src_len) {
  if (len + src_len + 1 > cap)
    return len;
  for (size_t i = 0; i < src_len; i++)
    dest[len++] = src[i];
  return len;
}

// U+00B7 MIDDLE DOT with a space either side, which is what separates the items.
const char SEPARATOR[] = " \xC2\xB7 ";

size_t append_item(char *dest, size_t len, size_t cap, const char *generic, const ControlLabel &label) {
  if (len > 0)
    len = append(dest, len, cap, SEPARATOR, sizeof(SEPARATOR) - 1);
  len = append(dest, len, cap, generic, strlen(generic));
  if (!label.is_default) {
    len = append(dest, len, cap, ": ", 2);
    len = append(dest, len, cap, label.text, label.length);
  }
  return len;
}

}  // namespace

EffectLabels effect_labels(const EffectInfo &info) {
  EffectLabels out;
  const char *at = strchr(info.metadata, '@');
  if (at == nullptr) {
    /* No metadata beyond the name. WLED shows the two standard sliders, all
     * three colour slots and the palette for an effect like this, so say the
     * same thing rather than claim the effect has no controls. */
    for (unsigned i = 0; i < 2; i++) {
      out.slider[i].text = "!";
      out.slider[i].length = 1;
      out.slider[i].is_default = true;
    }
    for (unsigned i = 0; i < 3; i++) {
      out.color[i].text = "!";
      out.color[i].length = 1;
      out.color[i].is_default = true;
    }
    out.palette.text = "!";
    out.palette.length = 1;
    out.palette.is_default = true;
    return out;
  }

  // Group 0 is five sliders followed by three checkmarks.
  ControlLabel controls[8];
  split_labels(at + 1, controls, 8);
  for (unsigned i = 0; i < 5; i++)
    out.slider[i] = controls[i];
  for (unsigned i = 0; i < 3; i++)
    out.check[i] = controls[5 + i];

  split_labels(group_at(info.metadata, 1), out.color, 3);

  ControlLabel palette[1];
  split_labels(group_at(info.metadata, 2), palette, 1);
  // A numeric palette group is a pinned palette, and WLED hides the selector.
  out.palette = all_digits(palette[0]) ? ControlLabel{} : palette[0];
  return out;
}

size_t format_effect_controls(const EffectInfo &info, char *dest, size_t dest_size) {
  if (dest == nullptr || dest_size == 0)
    return 0;
  const EffectLabels labels = effect_labels(info);
  size_t len = 0;
  for (unsigned i = 0; i < 5; i++) {
    if (labels.slider[i].used())
      len = append_item(dest, len, dest_size, SLIDER_LABEL_DEFAULTS[i], labels.slider[i]);
  }
  for (unsigned i = 0; i < 3; i++) {
    if (labels.check[i].used())
      len = append_item(dest, len, dest_size, CHECK_LABEL_DEFAULTS[i], labels.check[i]);
  }
  if (len == 0)
    len = append(dest, len, dest_size, "No controls", 11);
  dest[len] = '\0';
  return len;
}

size_t format_effect_colors(const EffectInfo &info, char *dest, size_t dest_size) {
  if (dest == nullptr || dest_size == 0)
    return 0;
  const EffectLabels labels = effect_labels(info);
  size_t len = 0;
  if (labels.palette.used()) {
    // "Palette" is the generic name here, so a "!" prints the WLED wording
    // rather than nothing: "uses the palette" is the thing worth saying.
    len = append(dest, len, dest_size, "Palette: ", 9);
    if (labels.palette.is_default)
      len = append(dest, len, dest_size, PALETTE_LABEL_DEFAULT, strlen(PALETTE_LABEL_DEFAULT));
    else
      len = append(dest, len, dest_size, labels.palette.text, labels.palette.length);
  }
  const char *const generic[3] = {"Color 1", "Color 2", "Color 3"};
  for (unsigned i = 0; i < 3; i++) {
    if (!labels.color[i].used())
      continue;
    len = append_item(dest, len, dest_size, generic[i], labels.color[i]);
    // A "!" colour slot still wants WLED's own name on it, because "Color 1"
    // alone does not say that it is the effect colour.
    if (labels.color[i].is_default) {
      len = append(dest, len, dest_size, ": ", 2);
      len = append(dest, len, dest_size, COLOR_LABEL_DEFAULTS[i], strlen(COLOR_LABEL_DEFAULTS[i]));
    }
  }
  if (len == 0)
    len = append(dest, len, dest_size, "No palette or colours", 21);
  dest[len] = '\0';
  return len;
}

size_t EffectRegistry::count() {
  size_t total = 0;
  for (unsigned g = 0; g < LINKED_EFFECT_GROUP_COUNT; g++)
    total += LINKED_EFFECT_GROUPS[g]->count;
  return total;
}

const EffectInfo *EffectRegistry::at(size_t index) {
  for (unsigned g = 0; g < LINKED_EFFECT_GROUP_COUNT; g++) {
    const EffectGroup &group = *LINKED_EFFECT_GROUPS[g];
    if (index < group.count)
      return &group.entries[index];
    index -= group.count;
  }
  return nullptr;
}

const EffectInfo *EffectRegistry::find(const char *name) {
  const int index = EffectRegistry::index_of(name);
  return index < 0 ? nullptr : EffectRegistry::at(static_cast<size_t>(index));
}

int EffectRegistry::index_of(const char *name) {
  int index = 0;
  for (unsigned g = 0; g < LINKED_EFFECT_GROUP_COUNT; g++) {
    const EffectGroup &group = *LINKED_EFFECT_GROUPS[g];
    for (size_t i = 0; i < group.count; i++, index++) {
      if (effect_name_equals(group.entries[i], name))
        return index;
    }
  }
  return -1;
}

}  // namespace wled_fx
}  // namespace esphome
