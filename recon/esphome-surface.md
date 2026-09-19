# ESPHome surface recon for a WLED effects engine port

Recon only. Nothing in any existing repo was modified.

Date: 2026-09-19

---

## 0. Which tree this describes

The pre-existing local checkout at `C:\Users\bharv\development\esphome\` is stale and was
**not** used for any finding below:

```
fa7c42511a22980f9cea9073b5ba68e8f9ce6ba3  Tue Jun 17 01:59:07 2025 +0100
  [i2s_audio] Bugfix: crashes when unlocking i2s bus multiple times (#9100)
origin  https://github.com/esphome/esphome.git   (branch dev)
```

That is over 15 months old. A fresh shallow clone of `dev` was made and is the tree every
path in this document refers to:

```
C:\Users\bharv\development\esphome-wled-fx\refs\esphome
c10c06eeb5ca7dbc49e7edf07a841bebbdc99b94  Sun Sep 20 00:44:33 2026 +1200
  [ci] Add code-review agent skill for GitHub Copilot (#19370)

esphome/const.py:  __version__ = "2026.10.0-dev"
```

The clone is shallow (1 commit), so no git-history questions can be answered from it.

Local build toolchain on this machine:

```
C:\Users\bharv\esphome-venv\Scripts\python.exe --version  ->  Python 3.13.5
esphome.const.__version__                                 ->  2026.8.2
```

---

## 1. Addressable light effects

### 1.1 File inventory

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\esphome\components\light\`

| File | Lines | What it holds |
| --- | --- | --- |
| `addressable_light.h` | 123 | `AddressableLight`, `AddressableLightState`, `AddressableLightTransformer` |
| `addressable_light_effect.h` | 390 | `AddressableLightEffect` base plus all 8 built-in addressable effects |
| `addressable_light_wrapper.h` | 122 | `AddressableLightWrapper`, presents a plain light as a 1-pixel addressable |
| `light_effect.h` | 58 | `LightEffect` base |
| `esp_color_view.h` | 108 | `ESPColorSettable`, `ESPColorView` |
| `esp_range_view.h` | 81 | `ESPRangeView`, `ESPRangeIterator` |
| `esp_hsv_color.h` | 34 | `ESPHSVColor` |
| `esp_color_correction.h` | 77 | `ESPColorCorrection` |
| `effects.py` | 580 | registry plus every built-in effect's schema and codegen |
| `types.py` | 93 | the `MockObjClass` exports external components import |
| `__init__.py` | 575 | light platform schemas, `register_light()` |

### 1.2 The C++ attachment point

`esphome/components/light/light_effect.h`:

```cpp
class LightEffect {
 public:
  explicit LightEffect(const char *name) : name_(name) {}
  virtual void start() {}
  virtual void start_internal() { this->start(); }
  virtual void stop() {}
  virtual void apply() = 0;
  StringRef get_name() const { return StringRef(this->name_); }
  virtual void init() {}
  void init_internal(LightState *state) { this->state_ = state; this->init(); }
  uint32_t get_index() const;
  bool is_active() const;
  LightState *get_light_state() const { return this->state_; }
 protected:
  LightState *state_{nullptr};
  const char *name_;
};
```

`esphome/components/light/addressable_light_effect.h`:

```cpp
class AddressableLightEffect : public LightEffect {
 public:
  explicit AddressableLightEffect(const char *name) : LightEffect(name) {}
  void start_internal() override {
    this->get_addressable_()->set_effect_active(true);
    this->get_addressable_()->clear_effect_data();
    this->start();
  }
  void stop() override { this->get_addressable_()->set_effect_active(false); }
  virtual void apply(AddressableLight &it, const Color &current_color) = 0;
  void apply() override {
    // not using any color correction etc. that will be handled by the addressable layer
    Color current_color = color_from_light_color_values(this->state_->remote_values);
    this->apply(*this->get_addressable_(), current_color);
  }
  uint32_t get_effect_index() const { return this->get_index(); }
  bool is_current_effect() const { return this->is_active() && this->get_addressable_()->is_effect_active(); }
 protected:
  AddressableLight *get_addressable_() const { return (AddressableLight *) this->state_->get_output(); }
};
```

That single virtual, `void apply(AddressableLight &it, const Color &current_color)`, is the
whole light-side contract, and the recommended attachment point for the 1D front end.

The same header ships two helpers the port can reuse directly, spiritual equivalents of
FastLED's `sin16`/`sin8`:

```cpp
inline static int16_t sin16_c(uint16_t theta);
inline static uint8_t half_sin8(uint8_t v);
```

### 1.3 The buffer

`esphome/components/light/addressable_light.h`:

```cpp
class AddressableLight : public LightOutput, public Component {
 public:
  virtual int32_t size() const = 0;
  ESPColorView operator[](int32_t index) const;
  ESPColorView get(int32_t index);
  virtual void clear_effect_data() = 0;
  ESPRangeView range(int32_t from, int32_t to);
  ESPRangeView all();
  ESPRangeIterator begin();  ESPRangeIterator end();
  void shift_left(int32_t amnt);  void shift_right(int32_t amnt);
  bool is_effect_active() const;  void set_effect_active(bool effect_active);
  std::unique_ptr<LightTransformer> create_default_transition() override;
  void set_correction(float red, float green, float blue, float white = 1.0f);
  void setup_state(LightState *state) override;
  void update_state(LightState *state) override;
  void schedule_show() { this->state_parent_->schedule_write_(); }
 protected:
  virtual ESPColorView get_view_internal(int32_t index) const = 0;
  ESPColorCorrection correction_{};
  LightState *state_parent_{nullptr};
  bool effect_active_{false};
};
```

The buffer is **1D only** and accessed one pixel at a time through `ESPColorView`, which is
a five-pointer proxy object, not a contiguous array:

```cpp
class ESPColorView : public ESPColorSettable {
 public:
  ESPColorView(uint8_t *red, uint8_t *green, uint8_t *blue, uint8_t *white, uint8_t *effect_data,
               const ESPColorCorrection *color_correction);
  ESPColorView &operator=(const Color &rhs);
  ESPColorView &operator=(const ESPHSVColor &rhs);
  void set(const Color &color) override { this->set_rgbw(color.r, color.g, color.b, color.w); }
  void set_red(uint8_t red) override { *this->red_ = this->color_correction_->color_correct_red(red); }
  ...
  Color get() const;              // un-corrected
  uint8_t get_red_raw() const;    // corrected, as written to the wire
  uint8_t get_effect_data() const;
  void set_effect_data(uint8_t effect_data) override;
  void fade_to_white(uint8_t amnt) override;  void fade_to_black(uint8_t amnt) override;
  void lighten(uint8_t delta) override;       void darken(uint8_t delta) override;
 protected:
  uint8_t *const red_; uint8_t *const green_; uint8_t *const blue_; uint8_t *const white_;
  uint8_t *const effect_data_;
  const ESPColorCorrection *color_correction_;
};
```

Consequences:

* **Every write goes through a colour-correction multiply and a gamma LUT.** Writing a
  32x32 frame per tick means 1024 calls into `color_correct_*`. WLED does its own gamma and
  brightness, so doing both double-corrects. Either drive `set_correction(1, 1, 1)` and skip
  the gamma table, or accept double gamma. There is no public way to bypass `ESPColorView`.
* There is **one spare byte per pixel** (`effect_data`) provided specifically for effects.
  WLED's `Segment::data` is arbitrary-length, so this is not a substitute, but it exists.

`esp_color_correction.h`:

```cpp
class ESPColorCorrection {
 public:
  void set_max_brightness(const Color &max_brightness);
  void set_local_brightness(uint8_t local_brightness);
  void set_gamma_table(const uint16_t *table);
  inline Color color_correct(Color color) const;
  inline uint8_t color_correct_red(uint8_t red) const {
    uint8_t res = esp_scale8_twice(red, this->max_brightness_.red, this->local_brightness_);
    return this->gamma_correct_(res);
  }
  Color color_uncorrect(Color color) const;
};
```

### 1.4 Color

**There is no `light::ESPColor` in dev any more.** A repo-wide grep for `ESPColor` not
followed by `View`/`Correction`/`Settable` returns zero hits. `esphome::Color` is the single
shared 4-byte RGBW type used by both the display stack and the light stack, so conversion
between the two worlds is a plain byte copy.

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\esphome\core\color.h`:

```cpp
inline static constexpr uint8_t esp_scale8(uint8_t i, uint8_t scale) {
  return (uint16_t(i) * (1 + uint16_t(scale))) / 256;
}
inline static constexpr uint8_t esp_scale8_twice(uint8_t i, uint8_t scale1, uint8_t scale2) {
  return (uint32_t(i) * (1 + uint32_t(scale1)) * (1 + uint32_t(scale2))) >> 16;
}

struct Color {
  union { struct { union {uint8_t r; uint8_t red;}; union {uint8_t g; uint8_t green;};
                   union {uint8_t b; uint8_t blue;}; union {uint8_t w; uint8_t white;}; };
          uint8_t raw[4]; uint32_t raw_32; };
  inline constexpr Color();
  inline constexpr Color(uint8_t red, uint8_t green, uint8_t blue);
  inline constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);
  inline explicit constexpr Color(uint32_t colorcode);   // 0xWWRRGGBB, matches WLED packing
  inline bool is_on();
  inline Color operator*(uint8_t scale) const;           // esp_scale8 per channel
  inline Color operator*(const Color &scale) const;      // per-channel scale
  inline Color operator+(const Color &add) const;        // saturating
  inline Color operator-(const Color &subtract) const;   // saturating
  inline Color operator~() const;
  static inline uint8_t blend_channel(uint8_t from, uint8_t to, uint8_t amnt);
  Color gradient(const Color &to_color, uint8_t amnt) const;
  Color fade_to_white(uint8_t amnt) const;  Color fade_to_black(uint8_t amnt) const;
  Color lighten(uint8_t delta);  Color darken(uint8_t delta);
  static Color random_color();
  static const Color BLACK;  static const Color WHITE;
};
```

`esp_hsv_color.h`: `struct ESPHSVColor { uint8_t h, s, v; Color to_rgb() const; }`, 8-bit
per channel like FastLED's `CHSV`.

### 1.5 How effects are registered in Python

`esphome/components/light/effects.py`:

```python
BINARY_EFFECTS = []
MONOCHROMATIC_EFFECTS = []
RGB_EFFECTS = []
ADDRESSABLE_EFFECTS = []

EFFECTS_REGISTRY = Registry()


def register_effect(name, effect_type, default_name, schema, *extra_validators):
    schema = schema if isinstance(schema, cv.Schema) else cv.Schema(schema)
    schema = schema.extend({cv.Optional(CONF_NAME, default=default_name): cv.string_strict})
    validator = cv.All(schema, *extra_validators)
    return EFFECTS_REGISTRY.register(name, effect_type, validator)


def register_addressable_effect(name, effect_type, default_name, schema, *extra_validators):
    # addressable effect can be used only in addressable
    ADDRESSABLE_EFFECTS.append(name)
    return register_effect(name, effect_type, default_name, schema, *extra_validators)
```

A typical registration:

```python
@register_addressable_effect(
    "addressable_rainbow",
    AddressableRainbowLightEffect,
    "Rainbow",
    {
        cv.Optional(CONF_SPEED, default=10): cv.uint32_t,
        cv.Optional(CONF_WIDTH, default=50): cv.int_range(min=1, max=65535),
    },
)
async def addressable_rainbow_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(var.set_speed(config[CONF_SPEED]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    return var
```

Parameters are an ordinary voluptuous dict; codegen returns a `new_Pvariable` whose first
constructor argument is always the effect's display name.

`validate_effects(allowed_effects)` (`effects.py:547`) rejects effects not allowed for that
light type and rejects duplicate names. No cap on effect count.

### 1.6 How an external component adds an addressable effect

All three in-tree examples do the same thing, and the imports work from an external
component with no special support.

`esphome/components/wled/__init__.py` (the UDP sync receiver, not an effects engine):

```python
import esphome.codegen as cg
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.const import CONF_NAME, CONF_PORT
from esphome.core import CORE, ID

wled_ns = cg.esphome_ns.namespace("wled")
WLEDLightEffect = wled_ns.class_("WLEDLightEffect", AddressableLightEffect)

CONFIG_SCHEMA = cv.All(cv.Schema({}), cv.only_with_arduino)

@register_addressable_effect("wled", WLEDLightEffect, "WLED", {...})
async def wled_light_effect_to_code(config, effect_id):
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    ...
    if CORE.is_esp32:
        cg.add_library("WiFi", None)
    return effect
```

`esphome/components/e131/__init__.py`, the pattern where the effect needs a shared hub:

```python
AUTO_LOAD = ["socket"]
DEPENDENCIES = ["network"]

E131AddressableLightEffect = e131_ns.class_("E131AddressableLightEffect", AddressableLightEffect)
E131Component = e131_ns.class_("E131Component", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(E131Component),
    cv.Optional(CONF_METHOD, default="MULTICAST"): cv.one_of(*METHODS, upper=True),
}).extend(cv.COMPONENT_SCHEMA)

@register_addressable_effect("e131", E131AddressableLightEffect, "E1.31", {
    cv.GenerateID(CONF_E131_ID): cv.use_id(E131Component),
    cv.Required(CONF_UNIVERSE): cv.int_range(min=1, max=512),
    cv.Optional(CONF_CHANNELS, default="RGB"): cv.one_of(*CHANNELS, upper=True),
})
async def e131_light_effect_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_E131_ID])
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(effect.set_e131(parent))
    return effect
```

`esphome/components/adalight/__init__.py`, an effect that is also a bus device:

```python
DEPENDENCIES = ["uart"]
AdalightLightEffect = adalight_ns.class_("AdalightLightEffect", uart.UARTDevice, AddressableLightEffect)

@register_addressable_effect("adalight", AdalightLightEffect, "Adalight",
                             {cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent)})
async def adalight_light_effect_to_code(config, effect_id):
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    await uart.register_uart_device(effect, config)
    return effect
```

**Loading gotcha.** `register_addressable_effect` is a decorator evaluated at module import
time, and ESPHome only imports a component's `__init__.py` when that component appears as a
top-level key in the YAML or is pulled in by `AUTO_LOAD`. That is why `wled` declares
`CONFIG_SCHEMA = cv.All(cv.Schema({}), cv.only_with_arduino)` despite having no options: the
user must write a bare `wled:` line for the effect to exist in the registry. A new
effects-engine component must do the same.

### 1.7 Update interval handling

`LightState::loop()` (`esphome/components/light/light_state.cpp:116`):

```cpp
void LightState::loop() {
  // Apply effect (if any)
  auto *effect = this->get_active_effect_();
  if (effect != nullptr) {
    effect->apply();
  }
  ...
  if (this->next_write_) {
    this->next_write_ = false;
    this->output_->write_state(this);
    this->disable_loop_if_idle_();
  }
}
```

There is **no framework-level rate limiting for effects**. `apply()` is called every pass of
the main loop while the effect is active; every effect self-throttles with the same idiom:

```cpp
const uint32_t now = millis();
if (now - this->last_update_ < this->update_interval_) return;
this->last_update_ = now;
```

`it.schedule_show()` sets `next_write_` via `state_parent_->schedule_write_()`, and the
strip write happens at the end of that same loop pass. `disable_loop_if_idle_()` parks the
component when no effect and no transformer is running, so the engine costs nothing when off.

Two `AGENTS.md` rules bind new code here:

> **Timing in `loop()`:** Never call `millis()` in a `loop()` body. The current tick's
> timestamp is already cached - use `App.get_loop_component_start_time()`.

> **The main loop runs every 16 ms.** A rate-limit gate shorter than that does nothing.

16 ms is 62.5 fps. The built-in effects still call `millis()` directly, so that rule is newer
than they are; new code will be held to it.

### 1.8 2D / matrix support in the light component

**There is none.** A full grep of `esphome/components/light/*.h` and `*.py` for `width`,
`height`, `matrix`, `2d` and x/y pairs returns exactly one hit, and it is the 1D rainbow
effect's hue span:

```
addressable_light_effect.h:88:    const uint16_t add = 0xFFFF / this->width_;
```

`AddressableLight` exposes only `int32_t size()`. No serpentine option, no panel count, no
x/y accessor anywhere in the light stack. All 2D geometry has to be invented, and the right
vocabulary to borrow is `width` / `height` / `pixel_mapper` from the `addressable_light`
display platform (section 2.4).

The closest remapping precedent is `esphome/components/partition/`:

```cpp
class AddressableSegment {
 public:
  AddressableSegment(light::LightState *src, int32_t src_offset, int32_t size, bool reversed);
};
class PartitionLightOutput final : public light::AddressableLight { ... };
```

Note `PartitionLightOutput`, `ESP32RMTLEDStripLightOutput` and the other concrete drivers are
declared `final`. Composition through `AddressableLight *` is the only route.

### 1.9 Existing addressable LED drivers (targets for the 1D front end)

`esp32_rmt_led_strip`, `spi_led_strip`, `beken_spi_led_strip`, `rp2040_pio_led_strip`,
`neopixelbus`, `fastled_clockless`, `fastled_spi`. All derive from `light::AddressableLight`,
so one front end covers them all.

---

## 2. Display side

### 2.1 `Display`

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\esphome\components\display\display.h`
(37 KB), namespace `esphome::display`:

```cpp
class Display : public PollingComponent {                        // line 317
 public:
  virtual void fill(Color color);
  virtual void clear();
  virtual int get_width()  { return this->get_width_internal(); }
  virtual int get_height() { return this->get_height_internal(); }
  int get_native_width();  int get_native_height();
  inline void draw_pixel_at(int x, int y) { this->draw_pixel_at(x, y, COLOR_ON); }
  virtual void draw_pixel_at(int x, int y, Color color) = 0;     // line 338
  virtual void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                              ColorOrder order, ColorBitness bitness, bool big_endian,
                              int x_offset, int y_offset, int x_pad);
  void set_writer(display_writer_t &&writer);                    // line 693
  void show_page(DisplayPage *page);  void show_next_page();  void show_prev_page();
  void set_pages(std::vector<DisplayPage *> pages);
  const DisplayPage *get_active_page() const;
  virtual void set_rotation(DisplayRotation rotation);
  void set_auto_clear(bool auto_clear_enabled);
  DisplayRotation get_rotation() const;
  virtual DisplayType get_display_type() = 0;
  bool is_clipping() const;
  bool ESPHOME_ALWAYS_INLINE is_point_clipped(int x, int y) const;
 protected:
  void do_update_();
  void ESPHOME_ALWAYS_INLINE feed_wdt_per_pixel_();   // only every 256th pixel really feeds
  virtual int get_height_internal() = 0;
  virtual int get_width_internal() = 0;               // line 796
  DisplayRotation rotation_{DISPLAY_ROTATION_0_DEGREES};
  display_writer_t writer_{};                         // line 808
  DisplayPage *page_{nullptr};
  bool auto_clear_enabled_{true};                     // line 812
  std::vector<Rect> clipping_rectangle_;
  uint8_t wdt_pixel_counter_{0};
};
```

**`do_update_()`, `writer_`, `auto_clear_enabled_`, `get_width_internal()` are all protected.**
An external component cannot call `do_update_()`. It gets `draw_pixel_at`, `draw_pixels_at`,
`fill`, `get_width`/`get_height`, `set_writer`, and `update()` where a subclass leaves it
public (hub75 does).

`do_update_()` is the entire render hook (`display.cpp:689`):

```cpp
void Display::do_update_() {
  if (this->auto_clear_enabled_) {
    this->clear();
  }
  if (this->show_test_card_) {
    this->test_card();
  } else if (this->page_ != nullptr) {
    this->page_->get_writer()(*this);
  } else if (this->writer_.has_value()) {
    (*this->writer_)(*this);
  }
  this->clear_clipping_();
}
```

`Display::fill()` defaults to a per-pixel loop through `filled_rectangle` ->
`horizontal_line` -> `draw_pixel_at`, so drivers that care about frame rate override it
(hub75 and pixoo both do). `Display::draw_pixels_at` also defaults to per-pixel decode, and
drivers override it.

**The writer type is no longer `std::function`** (new in dev, `display.h:189-288`):

```cpp
template<typename T> class DisplayWriter {
 public:
  // For stateless lambdas (convertible to function pointer): use function pointer (4 bytes)
  template<typename F>
  DisplayWriter(F f) requires std::invocable<F, T &> && std::convertible_to<F, void (*)(T &)>;
  // For stateful lambdas and std::function ... heap allocated
  // This handles backwards compatibility with external components
  template<typename F>
  DisplayWriter(F f) requires std::invocable<F, T &> &&(!std::convertible_to<F, void (*)(T &)>);
  bool has_value() const;
  void call(T &display) const;
  void operator()(T &display) const;
};
using display_writer_t = DisplayWriter<Display>;
```

A capturing lambda from an external component still works; it takes the heap `std::function`
path, and the header comment says that path exists for external components.

`display_buffer.h` (32 lines, whole file):

```cpp
class DisplayBuffer : public Display {
 public:
  int get_width() override;
  int get_height() override;
  void draw_pixel_at(int x, int y, Color color) override;
 protected:
  virtual void draw_absolute_pixel_internal(int x, int y, Color color) = 0;
  void init_internal_(uint32_t buffer_length);
  uint8_t *buffer_{nullptr};
};
```

`init_internal_` allocates via `RAMAllocator<uint8_t>`. `buffer_` is a raw `uint8_t*`; the
subclass decides bit depth.

Colour helpers (`display/display_color_utils.h`):

```cpp
enum ColorOrder : uint8_t { COLOR_ORDER_RGB = 0, COLOR_ORDER_BGR = 1, COLOR_ORDER_GRB = 2 };
enum ColorBitness : uint8_t { COLOR_BITNESS_888 = 0, COLOR_BITNESS_565 = 1, COLOR_BITNESS_332 = 2 };
```
plus `ColorUtil::to_color`, `color_to_565`, `color_to_332`, palette helpers.

### 2.2 Update mechanics

`display/__init__.py`:

```python
BASIC_DISPLAY_SCHEMA = cv.Schema(
    {cv.Exclusive(CONF_LAMBDA, CONF_LAMBDA): cv.lambda_}
).extend(cv.polling_component_schema("1s"))
```

So the **generic display default `update_interval` is 1 second**. `addressable_light`
overrides it to `16ms`. hub75 does not override it, so a hub75 display runs at 1 fps unless
the user sets `update_interval`.

`update_interval: never` becomes `SCHEDULER_DONT_RUN` (`4294967295`), and
`PollingComponent::start_poller()` registers the interval with that value, so `update()`
never fires. That is the supported way to hand a display to someone else.

`auto_clear_enabled` is three-valued in the schema:

```python
CONF_UNSPECIFIED = "unspecified"
cv.Optional(CONF_AUTO_CLEAR_ENABLED, default=CONF_UNSPECIFIED): validate_auto_clear,
...
if auto_clear == CONF_UNSPECIFIED:
    auto_clear = CONF_LAMBDA in config or CONF_PAGES in config
cg.add(var.set_auto_clear(auto_clear))
```

On a driver that does not override `fill()`, the auto-clear is a full per-pixel pass and
roughly doubles frame cost. **If the engine paints every pixel every frame, set
`auto_clear_enabled: false`.**

Codegen-time introspection of a display, which is how LVGL refuses to share one
(`display/__init__.py`):

```python
@dataclass(frozen=True)
class DisplayMetaData:
    width: int = 0
    height: int = 0
    has_hardware_rotation: bool = False
    byte_order: str = BYTE_ORDER_BIG
    has_writer: bool = False
    rotation: int = 0
    draw_rounding: int = 0

def get_display_metadata(display_id: ID) -> DisplayMetaData: ...
```

Caveat: **hub75 does not call `add_metadata`**, so `get_display_metadata` on a hub75 display
returns width/height 0. Geometry must come from `panel_width * layout_cols` at codegen time
or `get_width()`/`get_height()` at runtime.

### 2.3 hub75

**hub75 exists in dev** and landed after the stale local checkout was taken.

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\esphome\components\hub75\`:
`__init__.py` (229 B), `display.py` (23 KB), `hub75.cpp` (6.7 KB), `hub75_component.h` (2 KB),
`boards\{__init__.py, adafruit.py, apollo.py, huidu.py, trinity.py}`.

Codeowner: `CODEOWNERS:259: esphome/components/hub75/* @stuartparmenter`.

`__init__.py` in full, including a namespace trick worth knowing:

```python
from esphome.cpp_generator import MockObj

CODEOWNERS = ["@stuartparmenter"]

# Use fully-qualified namespace to avoid collision with external hub75 library's global ::hub75 namespace
hub75_ns = MockObj("::esphome::hub75", "::")
```

**Origin of the driver.** It is **not** ESP32-HUB75-MatrixPanel-I2S-DMA and not a PlatformIO
lib_dep. It is an ESPHome-owned ESP-IDF managed component (`display.py:564`):

```python
async def to_code(config: ConfigType) -> None:
    add_idf_component(
        name="esphome/esp-hub75",
        ref="0.3.5",
    )
```

`DEPENDENCIES = ["esp32"]`, all C++ wrapped in `#ifdef USE_ESP32`. The library exposes
global-namespace `Hub75Driver`, `Hub75Config`, `Hub75Pins`, `Hub75PixelFormat`,
`Hub75ColorOrder`, `Hub75ShiftDriver`, `Hub75PanelLayout`, `Hub75ScanWiring`,
`Hub75ClockSpeed`, `Hub75Rotation`, plus `HUB75_BIT_DEPTH` / `HUB75_GAMMA_MODE` build defines.

`hub75_component.h`, whole class:

```cpp
class HUB75Display final : public display::Display {
 public:
  explicit HUB75Display(const Hub75Config &config);
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  void update() override;
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }
  void fill(Color color) override;
  void draw_pixel_at(int x, int y, Color color) override;
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override;
  void set_brightness(uint8_t brightness);
 protected:
  int get_width_internal() override { return this->driver_ != nullptr ? this->driver_->get_width() : 0; }
  int get_height_internal() override { return this->driver_ != nullptr ? this->driver_->get_height() : 0; }
  Hub75Driver *driver_{nullptr};
  Hub75Config config_;
  uint8_t brightness_{128};
  bool enabled_{false};
};

template<typename... Ts> class SetBrightnessAction final : public Action<Ts...>, public Parented<HUB75Display> {
 public:
  TEMPLATABLE_VALUE(uint8_t, brightness)
  void play(const Ts &...x) override { this->parent_->set_brightness(this->brightness_.value(x...)); }
};
```

It derives from `display::Display`, **not** `DisplayBuffer`. There is no ESPHome-side
framebuffer at all; pixels go straight into the library's DMA buffer.

Draw path (`hub75.cpp`):

```cpp
void HUB75Display::update() {
  if (!driver_) [[unlikely]] return;
  if (!this->enabled_) [[unlikely]] return;
  this->do_update_();
  if (config_.double_buffer) {
    driver_->flip_buffer();
  }
}

void HOT HUB75Display::draw_pixel_at(int x, int y, Color color) {
  ...bounds and clipping checks...
  driver_->set_pixel(x, y, color.r, color.g, color.b);
  this->feed_wdt_per_pixel_();
}
```

`draw_pixels_at` is the bulk blit path and the one the engine wants:

```cpp
  const int stride_px = x_offset + w + x_pad;
  const bool is_packed = (x_offset == 0 && x_pad == 0 && y_offset == 0);

  if (is_packed) {
    // Tightly packed buffer - single bulk call for best performance
    driver_->draw_pixels(x_start, y_start, w, h, ptr, format, color_order, big_endian);
  } else {
    ...row by row...
  }
```

Accepted formats: `COLOR_BITNESS_565` -> `Hub75PixelFormat::RGB565` (2 bpp);
`COLOR_BITNESS_888` -> `RGB888` (3 bpp), or `RGB888_32` (4 bpp) under `USE_LVGL` with
`LV_COLOR_DEPTH == 32`. Anything else logs "Unsupported bitness" and returns.

Brightness:

```cpp
void HUB75Display::set_brightness(uint8_t brightness) {
  this->brightness_ = brightness;
  this->enabled_ = (brightness > 0);
  if (this->driver_ != nullptr) {
    this->driver_->set_brightness(brightness);
  }
}
```

Brightness 0 sets `enabled_ = false`, which short-circuits `update()`, `fill()`,
`draw_pixel_at()` and `draw_pixels_at()`. It is a hard stop-drawing gate, not a dim.

Config schema (`display.py`):

```python
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@stuartparmenter"]

HUB75Display = hub75_ns.class_("HUB75Display", cg.PollingComponent, display.Display)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(HUB75Display),
        cv.Optional(CONF_ROTATION): cv.enum(ROTATIONS, int=True),
        cv.Optional(CONF_BOARD): cv.one_of(*BOARDS.keys(), lower=True),
        cv.Required(CONF_PANEL_WIDTH): cv.positive_int,
        cv.Required(CONF_PANEL_HEIGHT): cv.positive_int,
        cv.Optional(CONF_LAYOUT_ROWS): cv.positive_int,
        cv.Optional(CONF_LAYOUT_COLS): cv.positive_int,
        cv.Optional(CONF_LAYOUT): cv.enum(PANEL_LAYOUTS, upper=True, space="_"),
        cv.Optional(CONF_SCAN_WIRING): _validate_scan_wiring,
        cv.Optional(CONF_SHIFT_DRIVER): cv.enum(SHIFT_DRIVERS, upper=True),
        cv.Optional(CONF_DOUBLE_BUFFER): cv.boolean,
        cv.Optional(CONF_BRIGHTNESS): cv.int_range(min=0, max=255),
        cv.Optional(CONF_BIT_DEPTH): cv.int_range(min=4, max=12),
        cv.Optional(CONF_GAMMA_CORRECT): cv.enum({"LINEAR": 0, "CIE1931": 1, "GAMMA_2_2": 2}, upper=True),
        cv.Optional(CONF_MIN_REFRESH_RATE): cv.int_range(min=40, max=200),
        # r1/g1/b1/r2/g2/b2, a/b/c/d/e, lat/oe/clk -> pins.gpio_output_pin_schema
        cv.Optional(CONF_CLOCK_SPEED): cv.enum(CLOCK_SPEEDS, upper=True),
        cv.Optional(CONF_LATCH_BLANKING): cv.positive_int,
        cv.Optional(CONF_CLOCK_PHASE): cv.boolean,
    }),
    _merge_board_pins,
    _validate_config,
)
```

* `PANEL_LAYOUTS`: HORIZONTAL, TOP_LEFT_DOWN, TOP_RIGHT_DOWN, BOTTOM_LEFT_UP,
  BOTTOM_RIGHT_UP, plus the four `_ZIGZAG` variants. This is where chain serpentine lives.
* `SHIFT_DRIVERS`: GENERIC, FM6126A, ICN2038S, FM6124, MBI5124, DP3246.
* `SCAN_WIRINGS`: STANDARD_TWO_SCAN, SCAN_1_4_16PX_HIGH, SCAN_1_8_32PX_HIGH,
  SCAN_1_8_32PX_FULL, SCAN_1_8_40PX_HIGH, SCAN_1_8_64PX_HIGH.
* `CLOCK_SPEEDS`: 8MHZ, 10MHZ, 16MHZ, 20MHZ.
* **`bit_depth` and `gamma_correct` are compile-time**, emitted as `-DHUB75_BIT_DEPTH=<n>`
  and `-DHUB75_GAMMA_MODE=<n>` build flags.

Frame rate: `min_refresh_rate` is the panel's PWM/BCM scan rate, not the ESPHome frame rate,
and is derived from `update_interval` when not given (`display.py:442`):

```python
DEFAULT_REFRESH_RATE = 60  # Hz

def _calculate_min_refresh_rate(config: ConfigType) -> int:
    """Priority:
    1. Explicit min_refresh_rate setting (user override)
    2. Derived from update_interval (ms to Hz conversion)
    3. Default 60 Hz (for LVGL or unspecified interval)
    """
```

`_final_validate` is the LVGL contract and is the template for what the effects engine should
demand of its display: ESP32-P4 requires `psram:`; with `lvgl:` present hub75 must have
`update_interval: never` ("LVGL manages its own refresh timing"), `auto_clear_enabled: false`
("LVGL manages screen clearing") and `double_buffer: false`.

Rotation is handled by the driver, and codegen forces the ESPHome-side rotation to 0 to avoid
double rotation.

**hub75 does not expose a light platform.** No `light/` subdir, no `light.py`. Brightness is
only the `hub75.set_brightness` automation action.

**Board presets include Apollo Automation M1.** `hub75/boards/apollo.py`, verbatim:

```python
"""Apollo Automation M1 board definitions."""

from . import BoardConfig

# Apollo Automation M1 Rev4
BoardConfig(
    "apollo-automation-m1-rev4",
    r1_pin=42, g1_pin=41, b1_pin=40, r2_pin=38, g2_pin=39, b2_pin=37,
    a_pin=45, b_pin=36, c_pin=48, d_pin=35, e_pin=21,
    lat_pin=47, oe_pin=14, clk_pin=2,
)

# Apollo Automation M1 Rev6
BoardConfig(
    "apollo-automation-m1-rev6",
    r1_pin=1, g1_pin=5, b1_pin=6, r2_pin=7, g2_pin=13, b2_pin=9,
    a_pin=16, b_pin=48, c_pin=47, d_pin=21, e_pin=38,
    lat_pin=8, oe_pin=4, clk_pin=18,
)
```

Other presets: `adafruit.py` (`adafruit-matrix-portal-s3`), `huidu.py`, `trinity.py`.

Tests: `tests/components/hub75/{common.yaml, test.esp32-idf.yaml, test.esp32-s3-idf.yaml,
test.esp32-s3-idf-board.yaml, test.esp32-s3-idf-rotate.yaml}`. All ESP-IDF; no Arduino leg.

### 2.4 `addressable_light` display platform (LED strip as a Display)

`esphome/components/addressable_light/`, the existing bridge, running in the opposite
direction to what the effects engine needs.

```python
CODEOWNERS = ["@justfalter"]

AddressableLightDisplay = addressable_light_ns.class_(
    "AddressableLightDisplay", display.DisplayBuffer, cg.PollingComponent
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(AddressableLightDisplay),
        cv.Required(CONF_ADDRESSABLE_LIGHT_ID): cv.use_id(light.AddressableLightState),
        cv.Required(CONF_WIDTH): cv.positive_int,
        cv.Required(CONF_HEIGHT): cv.positive_int,
        cv.Optional(CONF_UPDATE_INTERVAL, default="16ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PIXEL_MAPPER): cv.returning_lambda,
    }),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
)
```

`pixel_mapper` compiles as `[(int, "x"), (int, "y")] -> cg.int_`.

```cpp
class AddressableLightDisplay final : public display::DisplayBuffer {
 public:
  light::AddressableLight *get_light() const;
  void set_width(int32_t width);  void set_height(int32_t height);
  void set_light(light::LightState *state) {
    light_state_ = state;
    light_ = static_cast<light::AddressableLight *>(state->get_output());
  }
  void set_enabled(bool enabled) {
    if (light_state_) {
      if (enabled_ && !enabled) {  // enabled -> disabled
        // - Tell the parent light to refresh, effectively wiping the display. Also
        //   restores the previous effect (if any).
        if (this->last_effect_index_.has_value()) {
          light_state_->make_call().set_effect(*this->last_effect_index_).perform();
        }
      } else if (!enabled_ && enabled) {  // disabled -> enabled
        // - Save the current effect index.
        this->last_effect_index_ = light_state_->get_current_effect_index();
        // - Disable any current effect.
        light_state_->make_call().set_effect(uint32_t{0}).perform();
      }
    }
    enabled_ = enabled;
  }
  void set_pixel_mapper(std::function<int(int, int)> &&pixel_mapper_f);
 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  void update() override;
  std::vector<Color> addressable_light_buffer_;
  optional<uint32_t> last_effect_index_;
  optional<std::function<int(int, int)>> pixel_mapper_f_;
};
```

```cpp
void AddressableLightDisplay::update() {
  if (!this->enabled_) return;
  this->do_update_();
  this->display();
}

void AddressableLightDisplay::display() {
  bool dirty = false;
  ...
  for (uint32_t offset = 0; offset < this->addressable_light_buffer_.size(); offset++) {
    light::ESPColorView pixel = (*this->light_)[offset];
    // Track the original values for the pixel view. If it has changed updating, then
    // we trigger a redraw. Avoiding redraws == avoiding flicker!
    ...
    pixel.set_rgbw(c->r, c->g, c->b, c->w);
    ...
  }
  if (dirty) { this->light_->schedule_show(); }
}

void HOT AddressableLightDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  ...
  if (this->pixel_mapper_f_.has_value()) {
    int index = (*this->pixel_mapper_f_)(x, y);
    ...
    this->addressable_light_buffer_[index] = color;
  } else {
    this->addressable_light_buffer_[y * this->get_width_internal() + x] = color;
  }
}
```

Things to steal:

* `width` / `height` / `pixel_mapper` is the established ESPHome spelling for 2D geometry on
  a strip. Default mapping is row-major `y * width + x`; the lambda overrides it for
  serpentine wiring. The new component should reuse this vocabulary, not invent another.
* `update_interval` default `16ms` here is direct evidence that ESPHome's ordinary polling
  machinery is considered fine for 60 fps frame pushing.
* Dirty tracking before `schedule_show()`, explicitly to avoid flicker.
* `set_enabled()` deliberately fights the light's effect system: taking over means saving the
  current effect index and calling `set_effect(0)`, then restoring on release. **This is
  direct evidence that the light-effect system and the display system cannot both own a strip
  at once.** A WLED engine that is both a light effect and a display source must resolve this.

### 2.5 Other precedents

**`pixoo`** (`esphome/components/pixoo/`) is the closest existing thing to the target design:
a `display::Display` that owns a raw RGB888 frame buffer, blasts the whole frame every
`update()`, **and ships a brightness-only light platform**:

```cpp
class Pixoo : public display::Display, public spi::SPIDevice<...> {
  void update() override;
  void fill(Color color) override;
  void draw_pixel_at(int x, int y, Color color) override;
  void draw_pixels_at(...) override;
 protected:
  split_buffer::SplitBuffer buffer_{};
  uint8_t *frame_buffer_{nullptr};
};
```

```cpp
// pixoo/light/pixoo_light.h, whole file
// Brightness-only light that drives the Pixoo panel's LIGHT command.
class PixooLight : public light::LightOutput, public Parented<Pixoo> {
 public:
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
  }
  void write_state(light::LightState *state) override {
    float brightness;
    state->current_values_as_brightness(&brightness);
    this->parent_->set_panel_brightness(brightness);
  }
};
```

That is the exact pattern to copy if the engine should expose a light entity that dims an
arbitrary display (including hub75, which has no light of its own).

Its `setup()` also ends with a comment worth copying:

```cpp
  // Frames are pushed synchronously inside update(), so there is no loop() work to do and the
  // component is idle between updates. Marking it done (LOOP_DONE) lets LVGL's
  // update_when_display_idle option treat the panel as idle and drive frames on demand.
  this->disable_loop();
```

**`lvgl`** is the "draw every loop" mechanism, and it is not a display subclass. It is a
separate `Component` holding `std::vector<display::Display *> displays_` and overriding
`loop()`:

```cpp
class LvglComponent final : public PollingComponent {
 public:
  LvglComponent(std::vector<display::Display *> displays, float buffer_frac, bool full_refresh, ...);
  void setup() override;  void update() override;  void loop() override;
};

void LvglComponent::draw_buffer_(const lv_area_t *area, lv_color_data *ptr) {
  for (auto *display : this->displays_) {
    display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) ptr, display::COLOR_ORDER_RGB,
                            LV_BITNESS, this->big_endian_);
  }
}
```

and its refusal to share a display (`lvgl/__init__.py`):

```python
        metas = [get_display_metadata(disp) for disp in config[df.CONF_DISPLAYS]]
        if any(m.has_writer for m in metas):
            raise cv.Invalid(
                "Using lambda:, pages:, auto_clear_enabled: true, or show_test_card: true in display config is not compatible with LVGL"
            )
        if any(m.rotation != 0 for m in metas):
            raise cv.Invalid(
                "use of 'rotation' in the display config is not compatible with LVGL, please set rotation in the LVGL config instead"
            )
```

**`image` / `animation`** draw per pixel via `display->draw_pixel_at()` specifically so they
can honour transparency (`color.w < 0x80` skips). Not a model for 60 fps full frames.

**`mipi_spi`** re-implements `do_update_()` inline (auto_clear, page writer, writer_,
test_card) because it needs to run the writer once per horizontal band. That is the supported
escape hatch when `do_update_()` does not fit.

Full list of `Display`/`DisplayBuffer` subclasses in dev: addressable_light, hub75, ili9xxx,
inkplate, max7219digit, mipi_dsi, mipi_rgb, mipi_spi, pcd8544, pixoo, qspi_dbi, rpi_dpi_rgb,
sdl, snapshot/display, ssd1306_base, ssd1322_base, ssd1325_base, ssd1327_base, ssd1331_base,
ssd1351_base, st7567_base, st7701s, st7735, st7789v, st7920, waveshare_epaper.

### 2.6 The cleanest full-frame path, ranked

**Option A (recommended, the LVGL pattern).** Be a separate `Component` with `loop()` that
holds `display::Display *` and calls `draw_pixels_at()` once per frame.

* Schema: `cv.use_id(display.Display)` (or `cv.ensure_list` of them), `cg.get_variable` in
  `to_code`, pass the pointer to the constructor.
* `FINAL_VALIDATE`: copy hub75's and LVGL's contract. Require `update_interval: never` and
  `auto_clear_enabled: false` on the target display, and refuse when
  `get_display_metadata(disp).has_writer` is true. With `update_interval: never` the poller
  is `SCHEDULER_DONT_RUN`, so `update()` never fires and nothing fights the engine.
* Keep one packed `uint8_t` frame buffer (RGB888 or RGB565) and per frame issue:
  `disp->draw_pixels_at(0, 0, w, h, buf, display::COLOR_ORDER_RGB, display::COLOR_BITNESS_888, false);`
  On hub75 this hits the `is_packed` branch and becomes a single `driver_->draw_pixels(...)`
  straight into the DMA buffer with no intermediate copy. On a driver that has not overridden
  `draw_pixels_at`, the base decodes per pixel, which is slow but correct, so it degrades
  gracefully.
* Pace it in `loop()` off `App.get_loop_component_start_time()`, and use
  `disable_loop()`/`enable_loop()` when the engine is off.
* `do_update_()` is protected and unreachable from outside, which is fine because the engine
  is not using the writer/pages machinery at all.

**Option B.** `disp->set_writer([this](display::Display &it){ this->blit_(it); })` from
`setup()` plus `update_interval: 16ms`. Closest to `addressable_light`. Downsides: frame rate
is limited to the display's interval granularity, and `has_writer` stays false at codegen
time so other consumers (LVGL) will not know the display is taken.

**Option C.** Subclass the driver. Only needed for access to protected `do_update_()`,
`auto_clear_enabled_`, `writer_`. Unnecessary for hub75, whose whole surface is public.

**hub75-specific wrinkle:** `flip_buffer()` is only called inside `HUB75Display::update()`.
With Option A plus `update_interval: never`, double buffering never flips. Either use
`double_buffer: false`, or exploit that hub75's `update()` is public: set
`update_interval: never`, install a writer from C++, and have the engine's `loop()` call
`disp->update()` on a 16 or 33 ms deadline. That gets `do_update_()` plus the flip, and is
probably the cleanest hub75 answer.

---

## 3. Audio and FFT

### 3.1 Microphone API

`esphome/components/microphone/microphone.h`:

```cpp
namespace esphome::microphone {

enum State : uint8_t { STATE_STOPPED = 0, STATE_STARTING, STATE_RUNNING, STATE_STOPPING };

class Microphone {
 public:
  virtual void start() = 0;
  virtual void stop() = 0;
  template<typename F> void add_data_callback(F &&data_callback) {
    this->data_callbacks_.add([this, data_callback](const std::vector<uint8_t> &data) {
      if (this->mute_state_) { data_callback(std::vector<uint8_t>(data.size(), 0)); }
      else { data_callback(data); }
    });
  }
  bool is_running() const;  bool is_stopped() const;
  void set_mute_state(bool is_muted);  bool get_mute_state();
  audio::AudioStreamInfo get_audio_stream_info();
 protected:
  State state_{STATE_STOPPED};
  bool mute_state_{false};
  audio::AudioStreamInfo audio_stream_info_;
  CallbackManager<void(const std::vector<uint8_t> &)> data_callbacks_{};
};
```

No `set_data_callback`, no `MicrophoneSink`. The callback delivers raw interleaved PCM bytes
**on the I2S FreeRTOS task**, not the main loop.

The class new components should use is `MicrophoneSource`, whose header says:

> Components requesting microphone audio should register a callback through this class
> instead of registering a callback directly with the microphone if a particular format is
> required. ... Note that this class cannot convert sample rates!

```cpp
static const int32_t MAX_GAIN_FACTOR = 64;

class MicrophoneSource final {
 public:
  MicrophoneSource(Microphone *mic, uint8_t bits_per_sample, int32_t gain_factor, bool passive);
  void add_channel(uint8_t channel);
  template<typename F> void add_data_callback(F &&data_callback);
  void set_gain_factor(int32_t gain_factor);  int32_t get_gain_factor();
  audio::AudioStreamInfo get_audio_stream_info();
  void start();  void stop();
  bool is_passive() const;  bool is_running() const;  bool is_stopped() const;
 protected:
  void process_audio_(const std::vector<uint8_t> &data, std::vector<uint8_t> &filtered_data);
};
```

`process_audio_` does channel selection, bit-depth conversion and gain in Q25 fixed point.
No dithering, no resampling.

Python helpers (`microphone/__init__.py`):

```python
AUTO_LOAD = ["audio"]
IS_PLATFORM_COMPONENT = True

def microphone_source_schema(min_bits_per_sample=16, max_bits_per_sample=16,
                             min_channels=1, max_channels=1) -> cv.All: ...
async def microphone_source_to_code(config: ConfigType, passive: bool = False) -> MockObj: ...
def final_validate_microphone_source_schema(component_name, sample_rate=cv.UNDEFINED): ...
```

Actions/conditions: `microphone.capture`, `microphone.stop_capture`, `microphone.mute`,
`microphone.unmute`, `microphone.is_capturing`, `microphone.is_muted`. Trigger `on_data`.
Define `USE_MICROPHONE`.

### 3.2 i2s_audio

Already on the modern IDF driver (`driver/i2s_std.h`, `driver/i2s_pdm.h`, `i2s_chan_handle_t`,
`i2s_new_channel`, `i2s_channel_read`). No newer separate I2S component.

```python
# i2s_audio/__init__.py
CODEOWNERS = ["@jesserockz"]
DEPENDENCIES = ["esp32"]
MULTI_CONF = True
```

* ESP32 family only. No esp8266, rp2040, libretiny, nrf52 or host microphone at all.
* Bits per sample 8/16/24/32; channels `left`/`right`/`stereo` (`mono` rejected for mics).
* PDM only on esp32, esp32s3, esp32p4, and PDM forces 16 bits regardless of config.
* `adc_type: internal` is validated then rejected: "Internal ADC is no longer supported. Use
  an external I2S microphone instead."
* Mic defaults: `sample_rate 16000`, `channel right`, `bits_per_sample 32bit`.
* Producer task: `TASK_STACK_SIZE 4096`, `TASK_PRIORITY 23`, `READ_DURATION_MS 16`, DMA
  `.dma_desc_num = 4, .dma_frame_num = 256`. Start/stop is refcounted through a counting
  semaphore (`MAX_LISTENERS = 16`) so several consumers can share one mic.
* `_set_stream_limits` pins min == max, so `final_validate_microphone_source_schema(...,
  sample_rate=N)` hard-fails unless the mic is configured to match. No resampling on the mic
  path.
* All shipped test YAMLs are `-idf`.

### 3.3 FFT: none exists, but esp-dsp is already pinned

A full-tree search for `esp-dsp`, `esp_dsp`, `dsps_`, `kiss_fft`, `arduinoFFT` and `fft`
returns only:

| Path | What it is |
| --- | --- |
| `esphome/idf_component.yml` | `espressif/esp-dsp: version: "1.8.2"` |
| `esphome/components/esp32/__init__.py:296` | `"espressif__esp-dsp",  # DSP library - not used` inside `ARDUINO_EXCLUDED_IDF_COMPONENTS` |
| `esphome/components/runtime_image/__init__.py:106` | `add_idf_component(name="espressif/esp-dsp", ref="1.8.2")` for JPEGDEC S3 SIMD, not audio |
| `esphome/components/audio/audio.cpp:124` | a comment only, "the assembly dsps_mulc function has audio glitches..."; the function is a plain C loop |
| `tests/unit_tests/build_gen/test_espidf.py` | build-system unit tests |

No `dsps_fft2r` call, no `cg.add_library` for an FFT library, no `lib_deps` FFT entry.
**Nothing in ESPHome performs general real-time frequency analysis.** The complete inventory
of audio maths:

* `esphome/components/sound_level/` - time-domain peak and RMS in dBFS in `loop()`. Best
  structural template for a new audio-reactive component.
* `esphome/components/micro_wake_word/` - log-mel spectrogram via the bundled
  `esphome/esp-micro-speech-features` frontend (`<frontend.h>`, `FrontendProcessSamples`),
  hardwired to 40 mel bins, 30 ms window, 125 Hz to 7500 Hz, PCAN gain, int8 out for TFLite.
  It runs an FFT internally but exposes nothing reusable.
* `esp-audio-libs` - gain, PCM conversion, mixing, resampling. Time domain only.
* `i2s_audio_microphone::fix_dc_offset_` - one-pole DC blocker.

**The good news.** `espressif/esp-dsp: "1.8.2"` is already in `esphome/idf_component.yml`,
which `esphome/espidf/clang_tidy.py` reads to build the synthetic `main/idf_component.yml` so
headers resolve for linting, and which CI already knows about. Pulling it into a real firmware
build is one line, exactly as `runtime_image` does:

```python
from esphome.components import esp32
esp32.add_idf_component(name="espressif/esp-dsp", ref="1.8.2")
```

On Arduino this overwrites the `ARDUINO_EXCLUDED_IDF_COMPONENTS` stub. The runtime_image
comment says so: "On Arduino this overwrites the stub; on IDF it adds the component." So
`dsps_fft2r.h` / `dsps_wind_hann.h` are reachable on ESP32 under either framework with no new
third-party dependency and no vendoring. That removes the main dependency objection for the
audio-reactive tranche.

Caveat: `esphome/idf_component.yml` is one of the `CLANG_TIDY_GLOBAL_FILES`, so editing it
forces a **full-tree clang-tidy scan** on the PR. Using `add_idf_component` from the
component's own `__init__.py` avoids that, since the version is already pinned.

What still has to be written: windowing, magnitude, band binning, smoothing, AGC. And a
decision about where the FFT runs. An N=512 FFT inside `loop()` will stutter the LEDs; the
`micro_wake_word` pattern (a dedicated `StaticTask` from `esphome/core/static_task.h` with an
event group and an optional PSRAM stack) is the established way to move it off the main task.

### 3.4 Getting samples off the I2S task

There is no `esphome/core/ringbuffer.h`. The ring buffer is
`esphome/components/ring_buffer/ring_buffer.h`, a thin wrapper over the FreeRTOS
`RingbufHandle_t`, ESP32 only, single producer single consumer:

```cpp
class RingBuffer {
 public:
  size_t read(void *data, size_t len, TickType_t ticks_to_wait = 0);
  void *receive_acquire(size_t &length, size_t max_length, TickType_t ticks_to_wait = 0);
  void receive_release(void *item);
  size_t write(const void *data, size_t len);                 // overwrites oldest on overflow
  size_t write_without_replacement(const void *data, size_t len, TickType_t ticks_to_wait = 0,
                                   bool write_partial = true);
  size_t available() const;  size_t free() const;  BaseType_t reset();
  enum class MemoryPreference { EXTERNAL_FIRST, INTERNAL_FIRST };
  static std::unique_ptr<RingBuffer> create(size_t len,
      MemoryPreference preference = MemoryPreference::EXTERNAL_FIRST);
};
```

Zero-copy consumer, `esphome/components/audio/audio_transfer_buffer.h`:

```cpp
class AudioReadableBuffer {
 public:
  virtual const uint8_t *data() const = 0;
  virtual size_t available() const = 0;
  virtual void consume(size_t bytes) = 0;
  virtual bool has_buffered_data() const = 0;
  virtual size_t fill(TickType_t ticks_to_wait, bool pre_shift) { return 0; }
};

class RingBufferAudioSource : public AudioReadableBuffer {
 public:
  static std::unique_ptr<RingBufferAudioSource> create(
      std::shared_ptr<ring_buffer::RingBuffer> ring_buffer, size_t max_fill_bytes,
      uint8_t alignment_bytes = 1);
};
```

Passing `alignment_bytes = bytes_per_frame` makes every exposed region a whole number of
frames, stitching frames that straddle the wrap boundary.

The established handoff, from `sound_level.cpp`:

```cpp
void SoundLevelComponent::setup() {
  this->microphone_source_->add_data_callback([this](const std::vector<uint8_t> &data) {
    std::shared_ptr<ring_buffer::RingBuffer> temp_ring_buffer = this->ring_buffer_.lock();
    if (temp_ring_buffer != nullptr) {
      temp_ring_buffer->write((void *) data.data(), data.size());
    }
  });
  if (!this->microphone_source_->is_passive()) this->microphone_source_->start();
}
```

with `RING_BUFFER_DURATION_MS = 120`, `MAX_FILL_DURATION_MS = 30`, and a non-blocking
consumer in `loop()`:

```cpp
  this->audio_source_->fill(0, false);
  if (this->audio_source_->available() == 0) return;
  const int16_t *audio_data = reinterpret_cast<const int16_t *>(this->audio_source_->data());
  ...
  this->audio_source_->consume(stream_info.samples_to_bytes(samples_to_process));
```

`AudioStreamInfo` (`esphome/components/audio/audio.h`) provides all conversions:

```cpp
class AudioStreamInfo {
 public:
  AudioStreamInfo() : AudioStreamInfo(16, 1, 16000){};
  AudioStreamInfo(uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate);
  uint8_t get_bits_per_sample() const;  uint8_t get_channels() const;  uint32_t get_sample_rate() const;
  uint32_t bytes_to_ms(size_t bytes) const;       uint32_t bytes_to_frames(size_t bytes) const;
  uint32_t bytes_to_samples(size_t bytes) const;  size_t frames_to_bytes(uint32_t frames) const;
  size_t samples_to_bytes(uint32_t samples) const; uint32_t ms_to_frames(uint32_t ms) const;
  uint32_t ms_to_samples(uint32_t ms) const;      size_t ms_to_bytes(uint32_t ms) const;
};

inline int32_t unpack_audio_sample_to_q31(const uint8_t *data, size_t bytes_per_sample);
inline void pack_q31_as_audio_sample(int32_t sample, uint8_t *data, size_t bytes_per_sample);
```

---

## 4. Upstream contribution constraints

### 4.1 Licence

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\LICENSE`:

> The ESPHome License is made up of two base licenses: MIT and the GNU GENERAL PUBLIC LICENSE.
> The C++/runtime codebase of the ESPHome project (file extensions .c, .cpp, .h, .hpp, .tcc, .ino) are
> published under the GPLv3 license. The python codebase and all other parts of this codebase are
> published under the MIT license.

There is no `NOTICE`, `COPYING` or `AUTHORS` file, no CLA and no DCO. Third-party attribution
is done per component (section 4.6).

WLED (`wled/WLED` `LICENSE`):

> Copyright (c) 2016-present Christian Schwinne and individual WLED contributors
> Licensed under the EUPL v. 1.2 or later

`wled00/FX.h` header:

```
WS2812FX.h - Library for WS2812 LED effects.
Harm Aldick - 2016
www.aldick.org

Copyright (c) 2016  Harm Aldick
Licensed under the EUPL v. 1.2 or later
Adapted from code originally licensed under the MIT license

Modified for WLED

Segment class/struct (c) 2022 Blaz Kristan (@blazoncek)
```

**The licence path works, and it works because of the file split.** EUPL-1.2 Article 5:

> If the Licensee Distributes or Communicates Derivative Works or copies thereof based upon
> both the Work and another work licensed under a Compatible Licence, this Distribution or
> Communication can be done under the terms of this Compatible Licence.

and the EUPL-1.2 Appendix lists among the Compatible Licences:

> GNU General Public License (GPL) v. 2, v. 3

So WLED-derived C++ can be distributed inside ESPHome under GPLv3, which is exactly what
ESPHome's `.h`/`.cpp` already are. Attribution headers must be carried over.

**The trap:** ESPHome's Python is **MIT**. Every `__init__.py` / `display.py` / `light.py` in
the new component must be independent work. Effect name tables, parameter ranges, default
values and especially palette data transcribed from WLED into Python are a licence problem
that the C++ side does not have. Keep all WLED-derived tables in `.h` files.

### 4.2 AGENTS.md

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\AGENTS.md`, 885 lines, "ESPHome AI
Collaboration Guide". `CLAUDE.md`, `GEMINI.md` and `.github/copilot-instructions.md` are all
one-line pointers to it. `.claude/skills` and `.github/skills` point to `.agents/skills`.
There is no `.cursorrules`.

**There is no policy anywhere in the repo about AI-assisted pull requests.** The only AI
policy is about bug reports, in `.github/ISSUE_TEMPLATE/bug_report.yml`:

> "## Use of AI in bug reports
> AI tools are good at carrying out well-defined tasks, but they are not good at
> troubleshooting. Please do NOT paste an AI-generated wall of text into the issue template"
> "It is however quite acceptable to use AI to translate your *own* report"

Two agent skills exist:
`.agents/skills/pr-workflow/SKILL.md` ("Use `gh pr create` with the **full template** filled
in. Never skip or abbreviate sections.") and `.agents/skills/code-review/SKILL.md` ("Verify
the PR fills out `.github/PULL_REQUEST_TEMPLATE.md` and adds `CODEOWNERS` entries for a new
component.").

`.claude/settings.json` only defines a SessionStart hook that runs `script/setup`.

The rules that shape this design most:

Authority:

> **Read the developer documentation before writing a component.** https://developers.esphome.io
> ... it is the authority when they disagree.

Naming:

> C++: Follows the Google C++ Style Guide with these specifics (following clang-tidy conventions):
> - Function, method, and variable names: `lower_snake_case`
> - Class/struct/enum names: `UpperCamelCase`
> - Top-level constants (global/namespace scope): `UPPER_SNAKE_CASE`
> - Protected/private fields: `lower_snake_case_with_trailing_underscore_`
> - Enumerator names: prefix every value of an `enum class` with the enum name converted to
>   `UPPER_SNAKE_CASE` ... Never use bare names like `SUCCESS`, `FAILURE`, `OK`, or `FAIL`

That alone is a mechanical rewrite of essentially every identifier in WLED's `FX.cpp`
(`SEGMENT`, `SEGENV`, `strip`, `mode_static`, `WS2812FX`, `CRGBW`).

Memory, the single biggest constraint:

> **Heap allocation after `setup()` should be avoided unless absolutely unavoidable.** Every
> allocation/deallocation cycle contributes to fragmentation. ESPHome treats runtime heap
> allocation as a long-term reliability bug, not a performance issue.

> Use `std::array` instead of `std::vector` when size is known at compile time. ... For byte
> buffers: Avoid `std::vector<uint8_t>` unless the buffer needs to grow. Use
> `std::unique_ptr<uint8_t[]>` instead.

> **Avoid `std::deque`:** It allocates in 512-byte blocks regardless of element size...

with `StaticVector<T, N>` for compile-time-fixed lists (sized via `cg.add_define` /
`cg.slot_counter`), `FixedVector<T>` for runtime-known sizes, and `StringRef` for
config-set strings.

WLED's `Segment` does precisely the opposite: `uint32_t *pixels` allocated in the constructor,
`byte *data` via `bool allocateData(size_t len); void deallocateData();`, `char *name` freed
by `clearName()`, all torn down and reallocated whenever a segment resizes or an effect
changes. **Reconciling WLED's per-segment dynamic effect data with this rule is the hardest
single problem in the port.**

Loop timing (also section 1.7): no `millis()` in `loop()`; the main loop runs every 16 ms;
"under 250 ms use a gated `loop()`; 500 ms and above use `set_interval`".

Preprocessor:

> **Avoid `#define` for constants** ... Use `#define` only for conditional compilation and
> compile-time sizes calculated during Python code generation.

Constants:

> `esphome/const.py` is frozen -- do not add new `CONF_` constants there. Define a
> component-local constant in the component's own `.py`; for a constant shared by multiple
> components, add it to `esphome/components/const/__init__.py`. CI (`lint_constants_usage`)
> fails if the same constant is defined in three or more component files.

Comments and language:

> Code comments on individual lines should be used only where necessary to flag issues that
> may not be obvious on a simple reading of the code. Keep them short (e.g. 1 or 2 lines).

> The project uses English for non-code content. ... avoid technical jargon ... readily
> comprehensible to a wide audience, including non-native English speakers.

State in Python: use `CORE.data` with a `@dataclass` keyed under `DOMAIN`, never module-level
mutable globals ("Module-level globals persist between compilation runs if the host process
(e.g. device-builder) doesn't fork/exec").

Workflow:

> **Fork & Branch:** Create a new branch based on the `dev` branch (always use
> `git checkout -b <branch-name> dev`).

> Submit a PR against the `dev` branch. The Pull Request title must start with a `[tag]`
> prefix. For component work, use the component name (e.g., `[display] Fix bug`,
> `[abc123] Add new component`) ... Pull requests should always be made using the
> `.github/PULL_REQUEST_TEMPLATE.md` template - fill out all sections completely without
> removing any parts of the template.

> **Comments:** When commenting on GitHub PRs or issues, don't tag contributors, especially
> bots. Avoid referring to list items (e.g. from reviews) with the form #nn.

Documentation:

> Documentation is hosted in the separate `esphome/esphome.io` repository. ... When editing a
> component's documentation page, also update the corresponding component index page.

### 4.3 PR size is the biggest process risk

`.github/workflows/auto-label-pr.yml`:

```yaml
env:
  SMALL_PR_THRESHOLD: 30
  MEDIUM_PR_THRESHOLD: 100
  MAX_LABELS: 15
  TOO_BIG_THRESHOLD: 1000
  COMPONENT_LABEL_THRESHOLD: 10
```

`.github/scripts/auto-label-pr/detectors.js` `detectPRSize()` **excludes `tests/`** from the
count and uses net delta (additions minus deletions) for the too-big check:

```js
if (nonTestChanges > TOO_BIG_THRESHOLD && !isMegaPR) { labels.add('too-big'); }
```

`.github/scripts/auto-label-pr/reviews.js` turns that label into an automated
**REQUEST_CHANGES review**:

> "Hey @<author>, thanks for the contribution! Just a heads up, this PR is on the large side
> (N line changes excluding tests), which makes it harder for maintainers to review."
> "Smaller, focused PRs tend to be reviewed much faster ... If you can break this up into
> smaller pieces that can be reviewed independently, it will almost certainly land faster
> overall."
> "Before putting more time in, it's also worth popping into `#devs` on Discord so we can help
> you scope things and flag anything already in flight."
> "For more details (including how to split the work up), see:
> https://developers.esphome.io/contributing/submitting-your-work/#how-to-approach-large-submissions"

There is a `mega-pr` label a maintainer can apply that suppresses `too-big` and most auto
labelling. Also note `MAX_LABELS: 15`: exceeding it collapses everything to `too-big`.

Merge-blocking labels, `.github/workflows/status-check-labels.yml`:

```js
const blockingLabels = ['needs-docs', 'needs-developer-docs', 'merge-after-release', 'chained-pr'];
```

`needs-docs`, `needs-codeowners` and `needs-tests` are applied automatically:

* `new-component` is detected by an added file matching `^esphome/components/([^/]+)/__init__\.py$`,
  and only counts "when at least one newly added file defines a top-level CONFIG_SCHEMA, i.e.
  the new component/platform is actually loadable from YAML".
* If eligible and the PR body has no `https://github.com/esphome/esphome.io/pull/\d+` or
  `esphome/esphome.io#\d+` link, `needs-docs` is added, and the merge check fails.
* `new-component` with no `CODEOWNERS` addition adds `needs-codeowners`; with no `tests/`
  changes adds `needs-tests`.

PR title, `.github/workflows/pr-title-check.yml` ("Validate PR title") rejects
conventional-commit style:

> "PR title should not start with a \"prefix:\" style format.\nPlease use the format:
> [component] Brief description"

and rejects unbackticked `<`, `>`, `{`, `}`.

`.github/workflows/external-component-bot.yml` auto-comments the snippet for consuming the PR
as an external component:

```yaml
external_components:
  - source: github://pr#<number>
```

That is the mechanism for field testing staged PRs without merging.

### 4.4 CI jobs

Trigger: `push` to `dev`/`beta`/`release`, `pull_request`, `merge_group`. Most jobs are gated
on `script/determine-jobs.py`.

```
common, seed-apt-cache, determine-jobs, ci-custom, pylint, lint-format, pytest,
codecov-empty-upload, integration-tests, import-time, benchmarks, cpp-unit-tests,
clang-tidy-single, clang-tidy-nosplit, clang-tidy-split, clang-tidy-esp32-variants,
test-build-components-split, test-esp32-platformio, device-builder,
memory-impact-target-branch, memory-impact-pr-branch, memory-impact-comment, ci-status
```

`ci-custom` runs more than the lint script:

```
script/ci-custom.py
script/build_codeowners.py --check
script/build_alias_registry.py --check
script/build_language_schema.py --check
script/generate-esp32-boards.py --check
script/generate-rp2-boards.py --check
script/ci_check_duplicate_test_ids.py
script/ci_check_test_fixture_list_form.py
```

`ci_check_duplicate_test_ids.py` matters for a big component: every YAML `id:` across
`tests/components/` must be globally unique, because CI groups components into shared builds.

**There is no mypy.** `requirements_test.txt` pins `pylint==4.0.8`, `flake8==7.3.0`,
`ruff==0.16.7`, `pyupgrade==3.21.2`, `prek==0.5.3`. Pre-commit runs ruff + ruff-format,
flake8 (+flake8-docstrings), `no-commit-to-branch` for `dev`/`release`/`beta`,
end-of-file-fixer, trailing-whitespace, pyupgrade `--py312-plus`, yamllint, clang-format
(mirrors-clang-format v13.0.1), plus local pylint and ci-custom hooks.

`memory-impact-*` builds both branches and comments the RAM and flash delta on the PR. A port
of WLED's effect engine will produce a very visible flash number. Plan for it: gate effects
behind per-effect defines driven from the YAML so only listed effects compile in
(`cg.add_define`, `cg.slot_counter` are the existing idioms).

### 4.5 script/ci-custom.py rules that will bite a C++ port

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\script\ci-custom.py`, ~40 checks.
Most accept a trailing `// NOLINT`.

* `lint_pragma_once` - "Header file contains no 'pragma once' header guard."
* `lint_namespace` - "All integration C++ files should put all functions in a separate
  namespace that matches the integration's name." Accepts `namespace <name>` or
  `namespace esphome::<name>`.
* `lint_relative_cpp_import` - "Components must always use relative imports. Change:
  `#include \"esphome/components/abc/abc.h\"` to: `#include \"abc.h\"`". Same for Python.
* `lint_no_defines` - "#define macros for integer constants are not allowed, please use
  `static constexpr uint8_t NAME = value;` style instead". **Direct hit on WLED FX code**,
  which is dense with `#define FX_MODE_*` and `#define MODE_COUNT 220`.
* `lint_no_arduino_framework_functions` blocks `digitalWrite`, `digitalRead`, `pinMode`,
  `shiftOut`, `shiftIn`, `radians`, `degrees`, `interrupts`, `noInterrupts`, `lowByte`,
  `highByte`, `bitRead`, `bitSet`, `bitClear`, `bitWrite`, `bit`, `analogRead`, `analogWrite`,
  `pulseIn`, `pulseInLong`, `tone`. WLED uses several.
* `lint_no_byte_datatype` - "The datatype `byte` is not allowed ... Please use `uint8_t`".
  WLED's `Segment::data` is `byte *`.
* `lint_no_powf_in_core` - `powf()` banned in `esphome/core` and in all `BASE_ENTITY_PLATFORMS`,
  **and `light` is on that list**. "`powf()` pulls in __ieee754_powf (~2.3KB flash)". Use
  `pow10_int(exp)` or lookup tables. A separate `wled_fx` component directory escapes this.
* `lint_no_std_to_string`, `lint_no_heap_allocating_helpers` (bans `format_hex`, `str_sprintf`,
  `str_snprintf`, `str_upper_case`, `str_lower_case`), `lint_no_sprintf`, `lint_no_scanf`.
* `lint_no_std_string_view` - use `StringRef` from `esphome/core/string_ref.h`.
* `lint_no_std_bind` - "Lambdas are clearer, produce smaller binaries".
* `lint_log_in_header` - "Found reference to ESP_LOG in header file ... please move the
  definition to a source file (.cpp)". Header-only effect implementations cannot log.
* `lint_log_no_bare_literal_ternary` - wrap string-literal ternary branches in
  `LOG_STR_LITERAL(...)`.
* `lint_no_long_delays` - any `delay(n)` with n >= 50 fails.
* `lint_no_removed_in_idf_conversions` - `ARDUINO_ARCH_ESP32` -> `USE_ESP32`,
  `pgm_read_byte` -> `progmem_read_byte`, `ICACHE_RAM_ATTR` -> `IRAM_ATTR`.
* `lint_inclusive_language` - `whitelist`, `blacklist`, `slave` rejected case-insensitively.
* `lint_ino` / `lint_ext_check` - only registered extensions; no `.ino`.
* `lint_esphome_h` - no reference to `esphome.h`.
* **Windows hazards:** `lint_tabs`, `lint_newline` ("File contains Windows newline. Please set
  your editor to Unix newline mode."), `lint_trailing_whitespace`, `lint_end_newline`,
  `lint_executable_bit` ("If running from a windows machine please see disabling executable
  bit in git.").
* `lint_const_py_frozen` (with `CONST_PY_MAX_CONF = 1016` as a hard ceiling) and
  `lint_constants_usage`, whose message ends "... **in a separate PR**", a useful precedent
  for staging.

### 4.6 clang-format and clang-tidy

`.clang-format`: Google-based, `ColumnLimit: 120`, `IndentWidth: 2`, `UseTab: Never`,
`AccessModifierOffset: -1`, `PointerAlignment: Right`, `SortIncludes: false`,
`FixNamespaceComments: true`, `IndentCaseLabels: true`, `SpacesBeforeTrailingComments: 2`,
`AllowShortFunctionsOnASingleLine: All`. The `lint-format` job auto-pushes formatting fixes
back onto the PR branch.

`.clang-tidy` enables everything then subtracts (`-abseil-*`, `-altera-*`, `-android-*`,
`-boost-*`, `-hicpp-*`, `-llvmlibc-*`, `-concurrency-*`, `-mpi-*`, `-objc-*`). Two key lines:

```
WarningsAsErrors: '*'
FormatStyle:     google
```

`readability-identifier-naming` **is** active: `LocalVariableCase/ParameterCase/
StaticVariableCase: lower_case`; `ClassCase/StructCase/EnumCase: CamelCase`;
`EnumConstantCase/StaticConstantCase/GlobalConstantCase: UPPER_CASE`;
`FunctionCase/ClassMethodCase/VirtualMethodCase: lower_case`; `PrivateMemberCase: lower_case`
with `PrivateMemberSuffix: '_'` (same for protected). Also
`google-readability-function-size.StatementThreshold: '800'` and
`modernize-make-unique.MakeSmartPtrFunctionHeader: esphome/core/helpers.h`.

Helpfully **disabled**, so not blockers for a straight port: `google-build-using-namespace`,
`modernize-avoid-c-arrays`, `cppcoreguidelines-pro-type-cstyle-cast`,
`readability-magic-numbers`, `cppcoreguidelines-avoid-magic-numbers`,
`bugprone-narrowing-conversions`, `readability-implicit-bool-conversion`, `misc-no-recursion`,
`readability-function-cognitive-complexity`.

**There is no component-level clang-tidy exclusion.** `find . -name ".clang-tidy*"` returns
only the root file. There is no `.clang-tidy.hash`. `script/clang_tidy_hash.py` is not an
exclusion mechanism; its docstring says:

> "``CLANG_TIDY_GLOBAL_FILES`` (plus ``SDKCONFIG_DEFAULTS_PREFIX``) lists the files that
> influence clang-tidy output; ``script/determine-jobs.py`` runs a full scan when one changes."

Those global files are `.clang-tidy`, `script/clang-tidy`, `platformio.ini`,
`requirements_dev.txt`, `esphome/idf_component.yml`, `esphome/components/esp32/__init__.py`,
`esphome/components/nrf52/__init__.py`. Touching any of them forces a full-tree scan. The only
per-line escape is inline `// NOLINT(check-name)`.

### 4.7 Dependencies and vendoring

The only written rule is three bullets in `AGENTS.md` section 7:

> **Python:** When adding a new Python dependency, add it to the appropriate `requirements*.txt`
> file and `pyproject.toml`.
> **C++ / PlatformIO:** When adding a new C++ dependency, add it to `platformio.ini` and use
> `cg.add_library`.
> **Build Flags:** Use `cg.add_build_flag(...)`.

plus "Keep dependencies minimal", and from the developer docs:

> In general, we try to avoid use of external libraries.

**There is no stated rule anywhere about bundling third-party code, vendored sources or
licence compatibility.** The policy is de facto, expressed through practice:

1. **Vendor with attribution.** `esphome/components/http_request/httplib.h` is a full copy of
   cpp-httplib with a header comment ("NOTE: This is a copy of httplib.h from
   https://github.com/yhirose/cpp-httplib ... it was considered preferable to use it with as
   few changes as possible, to facilitate future updates") and the original MIT notice. It is
   then excluded by name in three `ci-custom.py` rules with the comment
   "# Vendored third-party library".
2. **Per-component LICENSE file.** `esphome/components/vl53l0x/LICENSE.txt` is the only one in
   the tree: "Most of the code in this integration is based on the VL53L0x library by Pololu
   ... Please see the top-level LICENSE.txt for information about ESPHome's license. The
   licenses for Pololu's and ST's software are included below." `vl53l0x_sensor.cpp` repeats a
   short block comment pointing at it.
3. **Publish the library separately under the `esphome-libs` org and pull it in.** This is the
   strongest precedent for this project, and **hub75 itself is the example**: `esphome/esp-hub75`
   is an ESPHome-owned ESP Component Registry package pulled in with
   `add_idf_component(name="esphome/esp-hub75", ref="0.3.5")`. Same for
   `esphome/esp-audio-libs`, `esphome/esp-micro-speech-features`, `esphome/micro-flac`, and so
   on. `gsl3670/touchscreen.py:50` states the principle: "Firmware blobs are published as
   release assets of the companion repository rather than vendored into the ESPHome source
   tree."

Option 3 keeps every PR far under the 1000-line too-big threshold; option 1 trips it
immediately. Given the GPLv3-on-C++ / MIT-on-Python split, a vendored GPL engine is fine in
`.cpp`/`.h`, but the `__init__.py` must be original either way.

Mechanisms:

```python
cg.add_library(name, version, repository=None)                       # esphome/cpp_generator.py:687
esp32.add_idf_component(*, name, repo=None, ref=None, path=None)     # esp32/__init__.py:772
```

### 4.8 FastLED

**FastLED still exists in dev** as `fastled_base`, `fastled_clockless`, `fastled_spi`, with
`CODEOWNERS = ["@OttoWinter"]` and tests under `tests/components/fastled_clockless/` and
`fastled_spi/`. It is **not** a general dependency and is soft-deprecated.

`esphome/components/fastled_base/__init__.py`:

```python
    if CORE.is_esp32:
        from esphome.components.esp32 import add_idf_component
        add_idf_component(
            name="fastled/FastLED",
            repo="https://github.com/FastLED/FastLED.git",
            ref="d44c800a9e876a8394caefc2ce4915dd96dac77b",
        )
        cg.add_library("SPI", None)
        # FastLED's RMT5 driver hard-codes intr_priority=3, which conflicts with
        # esphome's RMT channels (remote_transmitter etc., priority 0): the IDF
        # driver rejects FastLED's channel and show() then hangs ~3s with no
        # output. Override to 0 so it shares the interrupt. See #17063.
        cg.add_build_flag("-DFL_RMT5_INTERRUPT_LEVEL=0")
    else:
        cg.add_library("fastled/FastLED", "3.9.16")
```

`fastled_clockless/light.py` names its replacement:

```python
    cv.only_with_framework(
        frameworks=Framework.ARDUINO,
        suggestions={Framework.ESP_IDF: ("esp32_rmt_led_strip", "light/esp32_rmt_led_strip")},
    ),
    cv.require_framework_version(
        esp8266_arduino=cv.Version(2, 7, 4),
        esp32_arduino=cv.Version(99, 0, 0),
        max_version=True,
        extra_message="Please see note on documentation for FastLED",
    ),
```

**Confirmed: FastLED is not acceptable as a dependency of a generic component.** It is pinned
to a single git SHA because of an RMT interrupt-priority conflict with ESPHome's own RMT
users, it is Arduino-only, and the auto-label bot has a `deprecated-component` detector that
posts a warning review on PRs touching it. Any WLED code calling FastLED maths (`scale8`,
`sin8`, `blend8`, `CRGB`, `CHSV`, `ColorFromPalette`) must be reimplemented against ESPHome's
`esp_scale8`, `sin16_c`, `Color`, `ESPHSVColor`. WLED itself no longer depends on FastLED, so
this is mainly about not reintroducing it.

### 4.9 Tests

`AGENTS.md`:

```
tests/
├── test_build_components/
│   └── common/          # Shared bus packages (uart, i2c, spi, etc.)
└── components/[component]/
    ├── common.yaml          # Component-only config (no bus definitions)
    ├── test.esp32-idf.yaml      # config + compile
    ├── test.esp8266-ard.yaml    # config + compile
    ├── test-variant.esp32-idf.yaml  # variant test, config + compile
    ├── validate.esp32-idf.yaml      # config-only (never compiled)
    └── validate-legacy.esp32-idf.yaml  # config-only variant
```

> **Never define buses (uart, i2c, spi, modbus) directly in test YAML files** - always use
> packages from `test_build_components/common/`.

> All includes in test files must go through dict-style `packages:` so that batch grouping
> works correctly ... Never use list-style packages ... or top-level merge keys.

Canonical modern form (`tests/components/cst328/test.esp32-idf.yaml`):

```yaml
packages:
  i2c: !include ../../test_build_components/common/i2c/esp32-idf.yaml
  cst328: !include common.yaml
```

The package key must equal the bus directory name, enforced by
`lint_test_package_key_matches_bus`. A component with no bus only needs the
`<component>: !include common.yaml` key.

Note `e131` and `hub75` still use the legacy `<<: !include common.yaml` merge-key form, so the
in-tree examples are not all current. Follow `AGENTS.md`, not them.

Platform tokens come from `tests/test_build_components/build_components_base.<target>.yaml`:
`bk72xx-ard`, `esp32-ard`, `esp32-c2-idf`, `esp32-c3-ard`, `esp32-c3-idf`, `esp32-c5-idf`,
`esp32-c6-idf`, `esp32-c61-idf`, `esp32-h2-idf`, `esp32-idf`, `esp32-p4-idf`, `esp32-s2-ard`,
`esp32-s2-idf`, `esp32-s3-ard`, `esp32-s3-idf`, `esp8266-ard`, `host`, `ln882x-ard`,
`nrf52-adafruit`, `nrf52-mcumgr`, `nrf52-microbit`, `nrf52-xiao-ble`, `rp2040-ard`,
`rp2350-ard`, `rtl87xx-ard`. No file mandates a minimum set; the PR template's Test
Environment checkboxes and the `needs-tests` label are the practical pressure.

`tests/components/light/` also carries GoogleTest C++ unit tests
(`test_gamma_correction.cpp`, `test_light_call_brightness.cpp`) run by
`script/cpp_unit_test.py` with `-fsanitize=address -fsanitize=undefined -DESPHOME_DEBUG`.
**That is the right home for pure-logic WLED effect maths tests**, and it avoids the compile
cost of YAML build tests.

Validate-only optimisation, useful for schema-heavy staged PRs:

> When a PR's only edits to a component are `validate.*.yaml` files ... CI skips the compile
> stage for that component entirely and only runs config validation. This is decided in
> `script/determine-jobs.py` via `_component_change_is_validate_only`.

### 4.10 CODEOWNERS

`C:\Users\bharv\development\esphome-wled-fx\refs\esphome\CODEOWNERS` (repo root, not `.github/`):

```
# This file is generated by script/build_codeowners.py
# People marked here will be automatically requested for a review
# when the code that they own is touched.
#
# Every time an issue is created with a label corresponding to an integration,
# the integration's code owner is automatically notified.
```

Relevant lines:

```
esphome/components/hub75/*       @stuartparmenter
esphome/components/light/*       @esphome/core
esphome/components/microphone/*  @jesserockz @kahrendt
esphome/components/fastled_base/* @OttoWinter
```

`wled`, `e131` and `adalight` have **no** codeowner, that is, unmaintained legacy.
`light/*` owned by `@esphome/core` means any change to the light base classes needs core-team
review; an external effect using only the existing `AddressableLightEffect` API does not.

Generated from each component's `CODEOWNERS = ["@user"]` by `script/build_codeowners.py`;
CI runs it with `--check` and fails with "CODEOWNERS file is not up to date."

### 4.11 PR template

`.github/PULL_REQUEST_TEMPLATE.md`, verbatim:

```markdown
# What does this implement/fix?

<!-- Quick description and explanation of changes -->

## Types of changes

- [ ] Bugfix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] New developer-facing feature (adds functionality for component developers; no end-user configuration change)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected) — [policy](...)
- [ ] Developer breaking change (an API change that could break external components) — [policy](...)
- [ ] Undocumented C++ API change (removal or change of undocumented public methods that lambda users may depend on) — [policy](...)
- [ ] Code quality improvements to existing code or addition of tests
- [ ] Other

**Related issue or feature (if applicable):**

- fixes <link to issue>

**Pull request in [esphome.io](https://github.com/esphome/esphome.io) with documentation (if applicable):**

- esphome/esphome.io#<esphome.io PR number goes here>

**Pull request in [developers.esphome.io](https://github.com/esphome/developers.esphome.io) with developer documentation (if applicable):**

- esphome/developers.esphome.io#<developers.esphome.io PR number goes here>

## Test Environment

- [ ] ESP32
- [ ] ESP32 IDF
- [ ] ESP8266
- [ ] RP2040/RP2350
- [ ] BK72xx
- [ ] RTL87xx
- [ ] LN882x
- [ ] nRF52840

## Example entry for `config.yaml`:

```yaml
# Example config.yaml

```

## Checklist:
  - [ ] The code change is tested and works locally.
  - [ ] Tests have been added to verify that the new code works (under `tests/` folder).

If user exposed functionality or configuration variables are added/changed:
  - [ ] Documentation added/updated in [esphome.io](https://github.com/esphome/esphome.io).
```

Note the docs repo is `esphome/esphome.io`, not `esphome/esphome-docs`. The old name is still
accepted by the label bot "during the transition period".

Issue templates: only `bug_report.yml` and `config.yml`, `blank_issues_enabled: false`, and
feature requests are redirected to https://github.com/orgs/esphome/discussions.

### 4.12 Name collision

`esphome/components/wled/` already exists (the UDP sync receiver), so the new component cannot
be called `wled`. Free and plausible: `wled_fx`, `light_fx`, `fx_engine`. There is no lint rule
forcing snake_case on a component directory; the de facto constraint is that it must be a valid
Python package name, and all 668 components are lowercase snake_case. Whatever the name, the PR
tag becomes `[<name>]`.

---

## 5. Local build facts for this machine

* Compile from the venv at `C:\Users\bharv\esphome-venv`, via **PowerShell**, never the Bash
  tool (the ESP-IDF installer refuses MSys paths).
* That venv is **esphome 2026.8.2 / Python 3.13.5**; `dev` is `2026.10.0-dev`. Two releases of
  drift. Consequences:
  * An external component validated against this venv must target 2026.8.x APIs, or the dev
    checkout needs its own editable install.
  * `hub75` landed in `dev`. Check whether 2026.8.2 has it before planning hub75 work against
    the installed version.
  * Upstream PRs must be developed against a `dev` checkout regardless
    (`git checkout -b <branch-name> dev`).
* Keep working configs under a short path (`C:\tmp\...`) to stay clear of Windows path limits
  in the IDF build.
* **Windows-specific CI hazards** to configure before the first commit: CRLF line endings, tab
  characters and the git executable bit are all hard lint failures (`lint_newline`,
  `lint_tabs`, `lint_executable_bit`).
* No flashing, no `esphome run`, no serial port probing from automation. Config validation and
  `esphome compile` are fine.

---

## 6. Scope of the thing being ported

Sizes from WLED `main`, `wled00/`:

| File | Bytes |
| --- | --- |
| `FX.cpp` | 486,348 |
| `FX_fcn.cpp` | 103,852 |
| `FXparticleSystem.cpp` | 103,172 |
| `FX.h` | 55,949 |
| `colors.cpp` | 27,195 |
| `FXparticleSystem.h` | 24,311 |
| `FX_2Dfcn.cpp` | 23,956 |
| `wled_math.cpp` | 7,537 |

`FX.h` declares `#define MODE_COUNT 220`. The audio-reactive code is **not** in `wled00/`; it
lives in the `audioreactive` usermod directory.

`Segment` in `FX.h` has `setPixelColor(unsigned n, uint32_t c)`,
`setPixelColorXY(unsigned x, unsigned y, uint32_t c)`, `is2D()`, `startY`/`stopY`,
`width()`/`height()`, friend declarations for `ParticleSystem2D` and `ParticleSystem1D`, and
heap members `uint32_t *pixels`, `byte *data`, `char *name` with
`bool allocateData(size_t len)` / `void deallocateData()`. **That `Segment` abstraction, not
the individual effects, is the real porting surface.** Get `Segment` right against ESPHome's
no-heap-after-setup rule and the effects are mostly mechanical.

---

## 7. Recommended attachment points, in one place

**Light front end (1D, and 2D-on-a-strip).** Subclass `light::AddressableLightEffect` and
implement `void apply(AddressableLight &it, const Color &current_color)`. Register with
`register_addressable_effect` exactly as `wled`, `e131` and `adalight` do. Give the component a
bare top-level key so its module gets imported. Take `width`, `height` and an optional
`pixel_mapper` lambda, spelled as the `addressable_light` display platform spells them, so
users learn one geometry vocabulary. Self-throttle off `App.get_loop_component_start_time()`.
Decide early whether to neutralise `ESPColorCorrection` or accept double gamma.

**Display front end (hub75 and every other display).** Follow the LVGL pattern: a separate
`Component` with `loop()` that holds `display::Display *`, owns one packed RGB888 or RGB565
frame buffer, and issues a single
`draw_pixels_at(0, 0, w, h, buf, COLOR_ORDER_RGB, COLOR_BITNESS_888, false)` per frame. On
hub75 that becomes one `driver_->draw_pixels(...)` straight into the DMA framebuffer with no
intermediate copy; on any other driver it degrades to a correct per-pixel decode.
`FINAL_VALIDATE` should demand `update_interval: never` and `auto_clear_enabled: false` and
refuse a display whose `get_display_metadata(...).has_writer` is true, copying hub75's and
LVGL's own contracts. For hub75 with `double_buffer: true`, either turn double buffering off or
call the public `HUB75Display::update()` from the engine's loop so `flip_buffer()` still runs.
Do not try to make hub75 look like an `AddressableLight`: it has no light platform, and
`AddressableLightDisplay::set_enabled()` proves the light-effect and display systems already
fight over ownership of a strip. If a brightness entity is wanted, copy `pixoo`'s
`PixooLight`, a `light::LightOutput` with `ColorMode::BRIGHTNESS` and `Parented<T>`.

**Audio front end.** `microphone.microphone_source_schema()` plus
`microphone.microphone_source_to_code(config, passive=True)`, write into a
`ring_buffer::RingBuffer` from the mic callback, read through an `audio::RingBufferAudioSource`
on a dedicated `StaticTask`, FFT with `espressif/esp-dsp 1.8.2` pulled in via
`esp32.add_idf_component` from the component's own `__init__.py` (not by editing
`esphome/idf_component.yml`, which would force a full clang-tidy scan). ESP32 only, which is
fine because `microphone` is ESP32 only. Copy `sound_level`'s shape.

**Packaging.** Seriously consider publishing the engine as an ESPHome-owned managed component
(the `esphome/esp-hub75` model) rather than vendoring 800 KB of C++ into
`esphome/components/`. It is the difference between every PR tripping the 1000-line `too-big`
auto-REQUEST_CHANGES and every PR being a thin, reviewable ESPHome shim.

**Staging.** A sequence that does not trip the bots:

1. Pure-logic core (segment abstraction, colour maths, palettes, noise) under
   `esphome/components/<name>/` with GoogleTest files in `tests/components/<name>/*.cpp` and
   **no top-level `CONFIG_SCHEMA`**. No `CONFIG_SCHEMA` means no `new-component` detection and
   therefore no `needs-docs` block, and test files do not count toward the size threshold.
2. The stage that first adds a top-level `CONFIG_SCHEMA` must carry the
   `esphome/esphome.io#NNN` docs link in the PR body, a `CODEOWNERS = ["@bharvey88"]` entry
   regenerated with `script/build_codeowners.py`, and `tests/components/<name>/` YAML using
   dict-style `packages:`.
3. Effect batches after that, each under 1000 net non-test lines.
4. 2D geometry plus the display front end.
5. Particle system.
6. Audio reactive.

Talk to `#devs` on the ESPHome Discord before stage 1, which is literally what the too-big bot
message asks for, and ask about the `mega-pr` label for any stage that cannot be split. Run
`python3 script/run-in-env.py prek run` and `script/ci-custom.py` locally before every push,
or the `lint-format` job will push formatting commits onto the branch.
