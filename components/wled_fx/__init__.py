"""WLED effect engine for ESPHome.

This file is independent work. It contains no effect names, palette data,
parameter tables or other material copied from WLED: everything derived from WLED
lives in the C++ sources, which are GPLv3. See PORTING.md.
"""

from pathlib import Path
import re

from esphome import automation
import esphome.codegen as cg
from esphome.components import display
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.const import (
    CONF_DISPLAY_ID,
    CONF_GAMMA_CORRECT,
    CONF_HEIGHT,
    CONF_ID,
    CONF_NAME,
    CONF_TEXT,
    CONF_UPDATE_INTERVAL,
    CONF_WIDTH,
)
import esphome.final_validate as fv

CODEOWNERS = ["@bharvey88"]
DOMAIN = "wled_fx"
MULTI_CONF = True

CONF_WLED_FX_ID = "wled_fx_id"
CONF_EFFECTS = "effects"
CONF_EFFECT = "effect"
CONF_PALETTE = "palette"
CONF_SPEED = "speed"
CONF_INTENSITY = "intensity"
CONF_CUSTOM1 = "custom1"
CONF_CUSTOM2 = "custom2"
CONF_CUSTOM3 = "custom3"
CONF_CHECK1 = "check1"
CONF_CHECK2 = "check2"
CONF_CHECK3 = "check3"
CONF_SERPENTINE = "serpentine"
CONF_USE_LIGHT_COLOR = "use_light_color"
CONF_AUTO_CLEAR_ENABLED = "auto_clear_enabled"

# update_interval: never is stored as uint32_t max.
UPDATE_INTERVAL_NEVER = 4294967295

wled_fx_ns = cg.esphome_ns.namespace("wled_fx")
WledFxController = wled_fx_ns.class_("WledFxController")
WledFxDisplay = wled_fx_ns.class_(
    "WledFxDisplay", cg.PollingComponent, WledFxController
)
WledFxLightEffect = wled_fx_ns.class_(
    "WledFxLightEffect", AddressableLightEffect, WledFxController
)

SetEffectAction = wled_fx_ns.class_("SetEffectAction", automation.Action)
NextEffectAction = wled_fx_ns.class_("NextEffectAction", automation.Action)
SetPaletteAction = wled_fx_ns.class_("SetPaletteAction", automation.Action)
SetTextAction = wled_fx_ns.class_("SetTextAction", automation.Action)
SetSliderAction = wled_fx_ns.class_("SetSliderAction", automation.Action)
SetCheckAction = wled_fx_ns.class_("SetCheckAction", automation.Action)
ControlSlider = wled_fx_ns.enum("ControlSlider", is_class=True)
ControlCheck = wled_fx_ns.enum("ControlCheck", is_class=True)

SLIDERS = {
    CONF_SPEED: ControlSlider.CONTROL_SLIDER_SPEED,
    CONF_INTENSITY: ControlSlider.CONTROL_SLIDER_INTENSITY,
    CONF_CUSTOM1: ControlSlider.CONTROL_SLIDER_CUSTOM1,
    CONF_CUSTOM2: ControlSlider.CONTROL_SLIDER_CUSTOM2,
    CONF_CUSTOM3: ControlSlider.CONTROL_SLIDER_CUSTOM3,
}
CHECKS = {
    CONF_CHECK1: ControlCheck.CONTROL_CHECK_CHECK1,
    CONF_CHECK2: ControlCheck.CONTROL_CHECK_CHECK2,
    CONF_CHECK3: ControlCheck.CONTROL_CHECK_CHECK3,
}


def effect_macro(name: str) -> str:
    """Turns an effect name from YAML into the macro the C++ guard tests."""
    return "WLED_FX_FX_" + re.sub(r"[^A-Z0-9]+", "_", name.upper()).strip("_")


CONTROL_SCHEMA = {
    cv.Optional(CONF_EFFECT): cv.string,
    cv.Optional(CONF_PALETTE): cv.string,
    cv.Optional(CONF_SPEED): cv.int_range(min=0, max=255),
    cv.Optional(CONF_INTENSITY): cv.int_range(min=0, max=255),
    cv.Optional(CONF_CUSTOM1): cv.int_range(min=0, max=255),
    cv.Optional(CONF_CUSTOM2): cv.int_range(min=0, max=255),
    cv.Optional(CONF_CUSTOM3): cv.int_range(min=0, max=31),
    cv.Optional(CONF_CHECK1): cv.boolean,
    cv.Optional(CONF_CHECK2): cv.boolean,
    cv.Optional(CONF_CHECK3): cv.boolean,
    cv.Optional(CONF_TEXT): cv.string,
}

_ENTRY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WledFxDisplay),
        cv.Optional(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Optional(CONF_EFFECTS): cv.ensure_list(cv.string),
        cv.Optional(CONF_WIDTH): cv.positive_not_null_int,
        cv.Optional(CONF_HEIGHT): cv.positive_not_null_int,
        cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
        **CONTROL_SCHEMA,
    }
).extend(cv.polling_component_schema("33ms"))


def _validate_entry(config):
    if CONF_DISPLAY_ID not in config:
        for key in (CONF_WIDTH, CONF_HEIGHT):
            if key in config:
                raise cv.Invalid(
                    f"'{key}' only applies to a wled_fx entry that drives a display",
                    path=[key],
                )
    return config


CONFIG_SCHEMA = cv.ensure_list(cv.All(_ENTRY_SCHEMA, _validate_entry))


def _final_validate(config):
    """A display handed to wled_fx must not also be driven by its own poller."""
    full_config = fv.full_config.get()
    for entry in config:
        if CONF_DISPLAY_ID not in entry:
            continue
        path = full_config.get_path_for_id(entry[CONF_DISPLAY_ID])[:-1]
        display_config = full_config.get_config_for_path(path)
        interval = display_config.get(CONF_UPDATE_INTERVAL)
        interval_ms = (
            interval
            if isinstance(interval, int)
            else (interval.total_milliseconds if interval is not None else None)
        )
        if interval_ms != UPDATE_INTERVAL_NEVER:
            raise cv.Invalid(
                "A display driven by wled_fx must have 'update_interval: never'. "
                "wled_fx owns the frame clock and calls the display itself.",
                path=[CONF_DISPLAY_ID],
            )
        if display_config.get(CONF_AUTO_CLEAR_ENABLED):
            raise cv.Invalid(
                "A display driven by wled_fx must have 'auto_clear_enabled: false'. "
                "The engine paints every pixel every frame.",
                path=[CONF_DISPLAY_ID],
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


# Each effect translation unit declares the group it belongs to with exactly two
# lines, which is all this scan needs. See PORTING.md.
#   #define WLED_FX_GROUP_<ID> (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_A || ...)
#   const EffectGroup EFFECT_GROUP_<ID>{"<id>", ENTRIES, ...};
_GROUP_GUARD_RE = re.compile(
    r"#define\s+WLED_FX_GROUP_(\w+)\s*((?:[^\n\\]*\\\s*\n)*[^\n]*)"
)
_FX_MACRO_RE = re.compile(r"WLED_FX_FX_\w+")


def _discover_effect_groups() -> dict[str, set[str]]:
    """Maps each EFFECT_GROUP_* symbol to the effect macros it can provide."""
    groups: dict[str, set[str]] = {}
    for path in sorted(Path(__file__).parent.glob("wf_effects_*.cpp")):
        text = path.read_text(encoding="utf-8")
        for name, guard in _GROUP_GUARD_RE.findall(text):
            if f"EFFECT_GROUP_{name}" not in text:
                continue
            groups[f"EFFECT_GROUP_{name}"] = set(_FX_MACRO_RE.findall(guard))
    return groups


def _add_effect_selection(config):
    """Emits the compile-time allow-list and forces the effect groups to link.

    ESPHome builds the generated sources into a static library, so a translation
    unit nothing refers to is never pulled out of the archive and its registration
    object never runs. Naming the group symbols from the generated main.cpp is what
    keeps them in.
    """
    selected: list[str] = []
    for entry in config:
        selected.extend(entry.get(CONF_EFFECTS, []))
    macros = {effect_macro(name) for name in selected}
    all_effects = not macros

    if all_effects:
        cg.add_build_flag("-DWLED_FX_ALL_EFFECTS")
    else:
        for macro in sorted(macros):
            cg.add_build_flag(f"-D{macro}=1")

    linked = [
        symbol
        for symbol, provides in sorted(_discover_effect_groups().items())
        if all_effects or (provides & macros)
    ]
    if not linked:
        raise cv.Invalid(
            "The 'effects' list selected no effect that exists. "
            "Effect names are the WLED display names, for example 'Fire 2012'."
        )
    declarations = "".join(f"extern const EffectGroup {s};" for s in linked)
    references = ", ".join(f"&{s}" for s in linked)
    cg.add_global(
        cg.RawStatement(
            "namespace esphome { namespace wled_fx { struct EffectGroup; "
            f"{declarations} "
            f"const EffectGroup *const LINKED_EFFECT_GROUPS[] = {{{references}}}; "
            f"const unsigned LINKED_EFFECT_GROUP_COUNT = {len(linked)};"
            " } }"
        )
    )


async def apply_controls(var, config):
    """Applies the shared control keys to a controller instance."""
    if CONF_EFFECT in config:
        cg.add(var.set_effect_by_name(config[CONF_EFFECT]))
    if CONF_PALETTE in config:
        cg.add(var.set_palette_by_name(config[CONF_PALETTE]))
    if CONF_TEXT in config:
        cg.add(var.set_text_value(config[CONF_TEXT]))
    engine = var.engine()
    if CONF_SPEED in config:
        cg.add(engine.set_speed(config[CONF_SPEED]))
    if CONF_INTENSITY in config:
        cg.add(engine.set_intensity(config[CONF_INTENSITY]))
    if CONF_CUSTOM1 in config:
        cg.add(engine.set_custom1(config[CONF_CUSTOM1]))
    if CONF_CUSTOM2 in config:
        cg.add(engine.set_custom2(config[CONF_CUSTOM2]))
    if CONF_CUSTOM3 in config:
        cg.add(engine.set_custom3(config[CONF_CUSTOM3]))
    if CONF_CHECK1 in config:
        cg.add(engine.set_check1(config[CONF_CHECK1]))
    if CONF_CHECK2 in config:
        cg.add(engine.set_check2(config[CONF_CHECK2]))
    if CONF_CHECK3 in config:
        cg.add(engine.set_check3(config[CONF_CHECK3]))


async def to_code(config):
    _add_effect_selection(config)
    for entry in config:
        if CONF_DISPLAY_ID not in entry:
            continue
        var = cg.new_Pvariable(entry[CONF_ID])
        await cg.register_component(var, entry)
        target = await cg.get_variable(entry[CONF_DISPLAY_ID])
        cg.add(var.set_display(target))
        cg.add(
            var.set_dimensions(entry.get(CONF_WIDTH, 0), entry.get(CONF_HEIGHT, 0))
        )
        cg.add(var.set_gamma(entry[CONF_GAMMA_CORRECT]))
        await apply_controls(var, entry)


LIGHT_EFFECT_SCHEMA = {
    cv.Optional(CONF_ID): cv.declare_id(WledFxLightEffect),
    cv.Optional(CONF_WIDTH): cv.positive_not_null_int,
    cv.Optional(CONF_HEIGHT): cv.positive_not_null_int,
    cv.Optional(CONF_SERPENTINE, default=False): cv.boolean,
    cv.Optional(CONF_USE_LIGHT_COLOR, default=True): cv.boolean,
    cv.Optional(
        CONF_UPDATE_INTERVAL, default="33ms"
    ): cv.positive_time_period_milliseconds,
    **CONTROL_SCHEMA,
}


# --- actions -----------------------------------------------------------------

_PARENT_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(WledFxController)})


@automation.register_action(
    "wled_fx.set_effect",
    SetEffectAction,
    _PARENT_SCHEMA.extend({cv.Required(CONF_EFFECT): cv.templatable(cv.string)}),
    synchronous=True,
)
async def set_effect_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_effect(await cg.templatable(config[CONF_EFFECT], args, cg.std_string)))
    return var


@automation.register_action(
    "wled_fx.next_effect",
    NextEffectAction,
    automation.maybe_simple_id(_PARENT_SCHEMA),
    synchronous=True,
)
async def next_effect_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "wled_fx.set_palette",
    SetPaletteAction,
    _PARENT_SCHEMA.extend({cv.Required(CONF_PALETTE): cv.templatable(cv.string)}),
    synchronous=True,
)
async def set_palette_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(
        var.set_palette(await cg.templatable(config[CONF_PALETTE], args, cg.std_string))
    )
    return var


@automation.register_action(
    "wled_fx.set_text",
    SetTextAction,
    _PARENT_SCHEMA.extend({cv.Required(CONF_TEXT): cv.templatable(cv.string)}),
    synchronous=True,
)
async def set_text_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_text(await cg.templatable(config[CONF_TEXT], args, cg.std_string)))
    return var


def _register_slider_action(key, enum_value, maximum):
    @automation.register_action(
        f"wled_fx.set_{key}",
        SetSliderAction,
        _PARENT_SCHEMA.extend(
            {cv.Required(key): cv.templatable(cv.int_range(min=0, max=maximum))}
        ),
        synchronous=True,
    )
    async def _to_code(config, action_id, template_arg, args, key=key, enum_value=enum_value):
        var = cg.new_Pvariable(action_id, template_arg, enum_value)
        await cg.register_parented(var, config[CONF_ID])
        cg.add(var.set_value(await cg.templatable(config[key], args, cg.int_)))
        return var

    return _to_code


def _register_check_action(key, enum_value):
    @automation.register_action(
        f"wled_fx.set_{key}",
        SetCheckAction,
        _PARENT_SCHEMA.extend({cv.Required(key): cv.templatable(cv.boolean)}),
        synchronous=True,
    )
    async def _to_code(config, action_id, template_arg, args, key=key, enum_value=enum_value):
        var = cg.new_Pvariable(action_id, template_arg, enum_value)
        await cg.register_parented(var, config[CONF_ID])
        cg.add(var.set_value(await cg.templatable(config[key], args, cg.bool_)))
        return var

    return _to_code


for _key, _enum in SLIDERS.items():
    _register_slider_action(_key, _enum, 31 if _key == CONF_CUSTOM3 else 255)
for _key, _enum in CHECKS.items():
    _register_check_action(_key, _enum)


# --- light effect ---------------------------------------------------------------


@register_addressable_effect(
    "wled_fx",
    WledFxLightEffect,
    "WLED FX",
    LIGHT_EFFECT_SCHEMA,
)
async def wled_fx_light_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(config.get(CONF_ID, effect_id), config[CONF_NAME])
    cg.add(var.set_dimensions(config.get(CONF_WIDTH, 0), config.get(CONF_HEIGHT, 0)))
    cg.add(var.set_serpentine(config[CONF_SERPENTINE]))
    cg.add(var.set_use_light_color(config[CONF_USE_LIGHT_COLOR]))
    cg.add(var.set_frame_interval(config[CONF_UPDATE_INTERVAL]))
    await apply_controls(var, config)
    return var
