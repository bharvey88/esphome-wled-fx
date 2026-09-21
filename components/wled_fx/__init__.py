"""WLED effect engine for ESPHome.

This file is independent work. It holds no effect list, palette data or parameter
tables copied from WLED: the names it validates against are read out of the C++
sources at config time, and everything derived from WLED lives there, under
GPLv3. See PORTING.md.
"""

import logging
from pathlib import Path
import re

from esphome import automation
import esphome.codegen as cg
# Aliased, every one of them: this package has subpackages called number,
# switch, select and light, and importing one binds its name on this module
# and would shadow the ESPHome component of the same name.
from esphome.components import display, microphone
from esphome.components import light as light_component
from esphome.components import number as number_component
from esphome.components import select as select_component
from esphome.components import switch as switch_component
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.const import (
    CONF_BLUE,
    CONF_BRIGHTNESS,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_DISPLAY_ID,
    CONF_ENTITY_CATEGORY,
    CONF_GAMMA_CORRECT,
    CONF_GREEN,
    CONF_HEIGHT,
    CONF_ICON,
    CONF_ID,
    CONF_INITIAL_STATE,
    CONF_MICROPHONE,
    CONF_MODE,
    CONF_NAME,
    CONF_OUTPUT_ID,
    CONF_PLATFORM,
    CONF_RED,
    CONF_STATE,
    CONF_TEXT,
    CONF_TYPE,
    CONF_UPDATE_INTERVAL,
    CONF_WEB_SERVER,
    CONF_WIDTH,
    PLATFORM_ESP32,
)
import esphome.final_validate as fv

from .effect_index import (
    CHECK_KEYS,
    COLOR_KEYS,
    CONTROL_KEYS,
    PALETTE_KEY,
    SLIDER_KEYS,
    SLIDER_MAXIMUM,
    available_effects,
    controls_by_key,
    effect_macro,
    effect_names,
    metadata_for,
    one_dimensional_only,
    palette_names,
    suggestion,
    two_dimensional_only,
)

_LOGGER = logging.getLogger(__name__)

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
CONF_INCLUDE_1D_EFFECTS = "include_1d_effects"
CONF_AUTO_CLEAR_ENABLED = "auto_clear_enabled"
CONF_AUDIO = "audio"
CONF_AUDIO_ID = "audio_id"
CONF_GAIN = "gain"
CONF_SQUELCH = "squelch"
CONF_INPUT_LEVEL = "input_level"
CONF_AGC = "agc"
CONF_SCALING = "scaling"
CONF_LIMITER = "limiter"
CONF_ATTACK = "attack"
CONF_DECAY = "decay"
CONF_MIC_FILTER = "mic_filter"
CONF_BANDPASS = "bandpass"
CONF_PASSIVE = "passive"
CONF_TASK_IN_PSRAM = "task_in_psram"

# update_interval: never is stored as uint32_t max.
UPDATE_INTERVAL_NEVER = 4294967295

# WLED's FRAMETIME at its default WLED_FPS of 42, which is the frame period the
# effect bodies were written against. Kept in step with FRAMETIME in
# wf_segment.h and with frame_interval_ in wled_fx_light.h.
FRAMETIME = "23ms"
DEFAULT_FRAME_INTERVAL = cv.positive_time_period_milliseconds(FRAMETIME)

# WLED's own output gamma, applied to the finished frame in show()
# (wled00/FX_fcn.cpp:1723) and on by default there (wled00/wled.h:412 and :414).
# The display front end is the stage that stands in for show(), so it carries
# the same default. 1.0 turns the stage off, for an output that already applies
# a curve of its own.
DEFAULT_OUTPUT_GAMMA = 2.2

# Where somebody who installed this with external_components can actually read
# the effect and palette lists. A bare "see README.md" means nothing to them.
EFFECT_LIST_URL = "https://github.com/bharvey88/esphome-wled-fx#effects"

wled_fx_ns = cg.esphome_ns.namespace("wled_fx")
WledFxController = wled_fx_ns.class_("WledFxController")
WledFxDisplay = wled_fx_ns.class_("WledFxDisplay", cg.Component, WledFxController)
WledFxLightEffect = wled_fx_ns.class_(
    "WledFxLightEffect", AddressableLightEffect, WledFxController
)
WledFxAudioSource = wled_fx_ns.class_("WledFxAudioSource", cg.Component)

SetEffectAction = wled_fx_ns.class_("SetEffectAction", automation.Action)
NextEffectAction = wled_fx_ns.class_("NextEffectAction", automation.Action)
SetPaletteAction = wled_fx_ns.class_("SetPaletteAction", automation.Action)
SetTextAction = wled_fx_ns.class_("SetTextAction", automation.Action)
SetSliderAction = wled_fx_ns.class_("SetSliderAction", automation.Action)
SetCheckAction = wled_fx_ns.class_("SetCheckAction", automation.Action)
SetColorAction = wled_fx_ns.class_("SetColorAction", automation.Action)
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


# --- the entity classes -------------------------------------------------------
#
# Declared here rather than in each platform package, because `controls:` builds
# the same entities out of this file. One declaration each, so a generic
# "Custom 1" written under `number:` and a named "Spawning rate" built by
# `controls:` are the same C++ class with the same runtime behaviour.

WledFxNumber = wled_fx_ns.class_("WledFxNumber", number_component.Number, cg.Component)
WledFxNumberType = wled_fx_ns.enum("WledFxNumberType", is_class=True)
NUMBER_TYPES = {
    CONF_SPEED: WledFxNumberType.WLED_FX_NUMBER_TYPE_SPEED,
    CONF_INTENSITY: WledFxNumberType.WLED_FX_NUMBER_TYPE_INTENSITY,
    CONF_CUSTOM1: WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM1,
    CONF_CUSTOM2: WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM2,
    CONF_CUSTOM3: WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM3,
}

WledFxSwitch = wled_fx_ns.class_("WledFxSwitch", switch_component.Switch, cg.Component)
WledFxSwitchType = wled_fx_ns.enum("WledFxSwitchType", is_class=True)
SWITCH_TYPES = {
    CONF_CHECK1: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK1,
    CONF_CHECK2: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK2,
    CONF_CHECK3: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK3,
}

WledFxSelect = wled_fx_ns.class_("WledFxSelect", select_component.Select, cg.Component)
WledFxSelectType = wled_fx_ns.enum("WledFxSelectType", is_class=True)
SELECT_TYPES = {
    CONF_EFFECT: WledFxSelectType.WLED_FX_SELECT_TYPE_EFFECT,
    CONF_PALETTE: WledFxSelectType.WLED_FX_SELECT_TYPE_PALETTE,
}

WledFxColorLight = wled_fx_ns.class_(
    "WledFxColorLight", light_component.LightOutput, cg.Component
)
WledFxColorLightType = wled_fx_ns.enum("WledFxColorLightType", is_class=True)
COLOR_LIGHT_TYPES = {
    "color1": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR1,
    "color2": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR2,
    "color3": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR3,
}

# Segment::colors[0] in wf_segment.h, which is WLED's own DEFAULT_COLOR
# (wled00/FX.h:45): 0xFFA000, an amber with 160 of green. It has to be this
# value and not a rounder one, because the light writes it into the engine at
# boot and it is what every effect draws on palette "Default", and what the four
# dynamic palettes are built from.
DEFAULT_COLOR1 = (0xFF, 0xA0, 0x00)


def color_light_schema(slot: str, *, with_parent_id: bool = True) -> cv.Schema:
    """The schema for one colour slot's light.

    A fresh device comes up with exactly the colours the engine already had, so
    adding these entities does not change what any effect looks like: colour 1
    on at WLED's own primary, colour 2 and colour 3 black.

    `with_parent_id` is off for a light that `controls:` builds, which already
    knows its controller and must not make the user name one.
    """
    if slot == "color1":
        restore_mode = "RESTORE_DEFAULT_ON"
        initial_state = {
            CONF_STATE: True,
            CONF_BRIGHTNESS: 1.0,
            CONF_RED: DEFAULT_COLOR1[0] / 255,
            CONF_GREEN: DEFAULT_COLOR1[1] / 255,
            CONF_BLUE: DEFAULT_COLOR1[2] / 255,
        }
    else:
        restore_mode = "RESTORE_DEFAULT_OFF"
        initial_state = {CONF_STATE: False}
    parent = (
        {cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController)}
        if with_parent_id
        else {}
    )
    return (
        light_component.light_schema(
            WledFxColorLight, light_component.LightType.RGB, default_restore_mode=restore_mode
        )
        .extend(
            {
                **parent,
                # A colour slot is a value, not a lamp: the picker should hand
                # the engine the colour it shows, without a display gamma bent
                # into it and without a second of fading on the way.
                cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
                cv.Optional(
                    CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
                ): cv.positive_time_period_milliseconds,
                cv.Optional(CONF_INITIAL_STATE, default=initial_state): (
                    light_component.LIGHT_STATE_SCHEMA
                ),
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
    )


async def color_light_to_code(config, slot: str, parent):
    """Builds one colour slot's light and points it at its controller."""
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], COLOR_LIGHT_TYPES[slot])
    await light_component.register_light(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, parent)
    return var


def _known_effect(value):
    """An effect name that the C++ sources actually register.

    Without this a typo is accepted, `set_effect_by_name()` returns false at
    boot and the device quietly runs whatever effect it started with.
    """
    value = cv.string(value)
    names = effect_names()
    if any(value.casefold() == name.casefold() for name in names):
        return value
    raise cv.Invalid(
        f'"{value}" is not a WLED FX effect.{suggestion(value, names)} '
        "Names are the WLED display names; the full list is at "
        f"{EFFECT_LIST_URL}"
    )


def _known_palette(value):
    """A palette name that the C++ sources actually register."""
    value = cv.string(value)
    names = palette_names()
    if any(value.casefold() == name.casefold() for name in names):
        return value
    raise cv.Invalid(
        f'"{value}" is not a WLED FX palette.{suggestion(value, names)} '
        f"The full list is at {EFFECT_LIST_URL}"
    )


# --- what an output offers ---------------------------------------------------
#
# Which effects a front end offers depends on the shape of what it drives. The
# rule, and the reason for it, is on effect_available() in wf_registry.h; this
# is the same rule at config time, reading the same flags out of the same C++
# sources, so an effect the device would refuse is refused here with an
# explanation instead. tools/check_effect_names.py fails if the two ever drift.
#
#   1D output: an addressable light with no width and height. Offers the 1D
#              effects and the ones written for both. A 2D-only effect there is
#              a solid fill, and no option changes that.
#   2D output: any display, or a light given a width and a height. Offers the
#              2D-capable effects. The 1D-only ones are hidden until the
#              configuration asks for them with include_1d_effects.

# The lines that tell somebody what to do about it, rather than only what is
# wrong. Kept here so the light effect and the display front end say the same
# thing.
_OPT_IN_LINE = (
    f"Add '{CONF_INCLUDE_1D_EFFECTS}: true' to this entry to offer the 1D "
    "effects here as well; they run through WLED's own 1D to 2D mapping, "
    "which is what they have always done on a matrix."
)
_GIVE_GEOMETRY_LINE = (
    "Give this effect a 'width' and a 'height' to describe a matrix wired as "
    "one strip, or pick an effect that runs on a strip."
)


def _layout_name(two_dimensional: bool) -> str:
    return "a matrix" if two_dimensional else "a one dimensional strip"


def _unavailable_reason(name: str, two_dimensional: bool, include_1d: bool) -> str | None:
    """Why this output cannot offer the effect, or None when it can."""
    if name.casefold() in {n.casefold() for n in available_effects(two_dimensional, include_1d)}:
        return None
    if not two_dimensional:
        return (
            f'"{name}" only runs on a matrix, and this output is a one '
            "dimensional strip, where it renders a solid colour and nothing "
            f"else. {_GIVE_GEOMETRY_LINE}"
        )
    return (
        f'"{name}" is a 1D effect and this output is a matrix. WLED FX offers '
        "the 2D effects on a matrix, because a 1D effect stretched over a panel "
        f"is rarely what somebody meant. {_OPT_IN_LINE}"
    )


def _check_effect_fits(config, two_dimensional: bool, include_1d: bool):
    """Rejects an 'effect:' the configured layout cannot run."""
    effect = config.get(CONF_EFFECT)
    if effect is None:
        return
    if (reason := _unavailable_reason(effect, two_dimensional, include_1d)) is not None:
        raise cv.Invalid(reason, path=[CONF_EFFECT])


CONTROL_SCHEMA = {
    cv.Optional(CONF_EFFECT): _known_effect,
    cv.Optional(CONF_PALETTE): _known_palette,
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

# The same keys as plain strings, for the checks that look inside a validated
# config rather than build a schema out of them.
_CONTROL_KEYS = (
    CONF_EFFECT,
    CONF_PALETTE,
    CONF_SPEED,
    CONF_INTENSITY,
    CONF_CUSTOM1,
    CONF_CUSTOM2,
    CONF_CUSTOM3,
    CONF_CHECK1,
    CONF_CHECK2,
    CONF_CHECK3,
    CONF_TEXT,
)

# --- named controls ----------------------------------------------------------
#
# The eight generic controls are called what WLED calls them for whichever
# effect is running, and an ESPHome entity's name is fixed when the firmware is
# built. Those two facts cannot both be served, so a configuration that pins one
# effect can ask for the other trade instead: `controls: true` builds, at
# compile time, one entity per control that effect actually uses, named what
# WLED names it, and nothing called Custom 1 or Check 1.
#
# It is for one pinned effect and nothing else. A build that switches between
# effects keeps the generic entities and the `controls` text sensor that says
# what they mean, and the errors below say so rather than half working.

CONF_CONTROLS = "controls"
CONF_NAME_PREFIX = "name_prefix"
CONF_RESTORE_VALUE = "restore_value"

# What the colour 1 light is called on a display when the effect does not use a
# colour slot of its own. That entity still has to exist there: on the display
# front end colour 1 carries the master brightness and the on/off for the whole
# panel, which is WLED's own arrangement, so a panel without it cannot be dimmed
# or blanked.
MASTER_COLOR_NAME = "Panel"

# Which platform each kind of control needs in the build. `controls:` is the
# only thing in the component that creates entities out of nowhere, so it is the
# only thing that has to ask for the platforms; AUTO_LOAD below returns exactly
# these and only when a configuration used them.
_CONTROL_PLATFORMS = {
    **{key: "number.wled_fx" for key in SLIDER_KEYS},
    **{key: "switch.wled_fx" for key in CHECK_KEYS},
    PALETTE_KEY: "select.wled_fx",
    **{key: "light.wled_fx" for key in COLOR_KEYS},
}

# The keys of `controls:` that adjust one entity. Everything here is forwarded
# to the entity's own schema, which is what validates it, so `web_server:` takes
# whatever sorting fields the installed ESPHome supports and says so itself when
# it does not.
_CONTROL_OVERRIDE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_ICON): cv.icon,
        cv.Optional(CONF_ENTITY_CATEGORY): cv.entity_category,
        cv.Optional(CONF_RESTORE_VALUE): cv.boolean,
        cv.Optional(CONF_WEB_SERVER): dict,
    }
)


def _control_option(value):
    """One control: `false` leaves it out, `true` takes it as it comes."""
    if isinstance(value, bool):
        return {} if value else False
    return _CONTROL_OVERRIDE_SCHEMA(value)


_CONTROLS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_NAME_PREFIX): cv.string,
        cv.Optional(CONF_ICON): cv.icon,
        cv.Optional(CONF_ENTITY_CATEGORY): cv.entity_category,
        cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
        cv.Optional(CONF_WEB_SERVER): dict,
        **{cv.Optional(key): _control_option for key in CONTROL_KEYS},
    }
)


def _controls_option(value):
    """`controls:` is a flag or a mapping; which control keys are legal in the
    mapping depends on the pinned effect, so that part waits for _expand()."""
    if isinstance(value, bool):
        return _CONTROLS_SCHEMA({}) if value else None
    return _CONTROLS_SCHEMA(value)


_NO_EFFECT_LINE = (
    f"'{CONF_CONTROLS}' builds one entity per control of one effect, so the "
    f"entry it is on needs exactly one '{CONF_EFFECT}:'. Name the effect, or "
    f"drop '{CONF_CONTROLS}' and use the generic 'number', 'switch' and "
    "'select' platforms, which work whatever effect is running."
)


def _control_entities(
    controls, effect: str, *, display: bool, use_light_color: bool, base: str | None
):
    """The validated entity configuration for every control the effect uses.

    One entry per entity, keyed by the control it drives, in WLED's own order.
    Everything about what exists and what it is called comes from the effect's
    metadata string, which is read out of the C++ registration tables; nothing
    here holds a table of effect parameters of its own.
    """
    available = controls_by_key(metadata_for(effect))

    if display and "color1" not in available:
        # Colour 1 is the panel's master brightness and its on/off switch, so a
        # display gets one whatever the effect does with the colour slot.
        from .effect_index import Control

        available["color1"] = Control(
            "color1", "color", MASTER_COLOR_NAME, False, None
        )
    if not display and use_light_color:
        # The light's own colour is segment colour 1 here, so a second entity
        # for it would be two controls fighting over one value.
        available.pop("color1", None)

    named = [key for key in controls if key in CONTROL_KEYS]
    unknown = [key for key in named if key not in available]
    if unknown:
        raise cv.Invalid(
            f'"{effect}" does not use {_and_list(unknown)}, so there is no '
            f"entity to configure. The controls it has are "
            f"{_and_list(list(available))}.",
            path=[CONF_CONTROLS, unknown[0]],
        )

    out = {}
    for key, control in available.items():
        override = controls.get(key, {})
        if override is False:
            continue
        out[key] = _entity_config(key, control, controls, override, base)
    return out


def _and_list(items) -> str:
    items = [f"'{item}'" for item in items]
    if len(items) < 2:
        return items[0] if items else "nothing"
    return f"{', '.join(items[:-1])} and {items[-1]}"


def _entity_name(control, controls, override) -> str:
    name = override.get(CONF_NAME, control.label)
    prefix = controls.get(CONF_NAME_PREFIX)
    return f"{prefix} {name}" if prefix else name


def _shared(controls, override) -> dict:
    """The keys `controls:` passes straight through to the entity's schema."""
    out = {}
    for key in (CONF_ICON, CONF_ENTITY_CATEGORY, CONF_WEB_SERVER):
        if key in override:
            out[key] = override[key]
        elif key in controls:
            out[key] = controls[key]
    return out


def _entity_config(key, control, controls, override, base: str | None):
    raw = {CONF_NAME: _entity_name(control, controls, override), **_shared(controls, override)}
    restore = override.get(CONF_RESTORE_VALUE, controls[CONF_RESTORE_VALUE])

    if control.kind == "color":
        if CONF_RESTORE_VALUE in override:
            raise cv.Invalid(
                f"'{CONF_RESTORE_VALUE}' does not apply to a colour slot. An "
                "ESPHome light restores its own state, so a colour comes back "
                "as it was left whatever this says.",
                path=[CONF_CONTROLS, key, CONF_RESTORE_VALUE],
            )
        if base is not None:
            raw[CONF_ID] = f"{base}_{key}"
            raw[CONF_OUTPUT_ID] = f"{base}_{key}_output"
        return color_light_schema(key, with_parent_id=False)(raw)

    if base is not None:
        raw[CONF_ID] = f"{base}_{key}"

    if control.kind == "slider":
        # A slider, because that is what it is in WLED, and 0 to 255 in a box is
        # not something anybody wants to type.
        raw.setdefault(CONF_MODE, "SLIDER")
        raw[CONF_RESTORE_VALUE] = restore
        return _NAMED_NUMBER_SCHEMA(raw)

    if control.kind == "check":
        # The engine owns this checkmark unless the configuration asked for it
        # to be remembered, and then the remembered value has to be the one
        # that wins at boot. Which of the two restore modes depends on what the
        # effect's own metadata says the checkmark starts at.
        mode = "DISABLED"
        if restore:
            mode = "RESTORE_DEFAULT_ON" if control.default else "RESTORE_DEFAULT_OFF"
        return switch_component.switch_schema(WledFxSwitch, default_restore_mode=mode).extend(
            cv.COMPONENT_SCHEMA
        )(raw)

    raw[CONF_RESTORE_VALUE] = restore
    return _NAMED_SELECT_SCHEMA(raw)


_NAMED_NUMBER_SCHEMA = (
    number_component.number_schema(WledFxNumber)
    .extend(cv.COMPONENT_SCHEMA)
    .extend({cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean})
)
_NAMED_SELECT_SCHEMA = (
    select_component.select_schema(WledFxSelect)
    .extend(cv.COMPONENT_SCHEMA)
    .extend({cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean})
)


def _expand_controls(config, *, display: bool, use_light_color: bool):
    """Turns `controls:` into the entity configurations it stands for."""
    controls = config.get(CONF_CONTROLS)
    if controls is None:
        return config
    effect = config.get(CONF_EFFECT)
    if effect is None:
        raise cv.Invalid(_NO_EFFECT_LINE, path=[CONF_CONTROLS])
    declared = config.get(CONF_ID)
    # Entity ids are only C++ variable names, and what Home Assistant shows is
    # built from the entity's name, so an entry with no id of its own can let
    # ESPHome generate them. An entry that has one gets readable ones.
    base = declared.id if declared is not None and declared.id else None
    config[CONF_CONTROLS] = _control_entities(
        controls,
        effect,
        display=display,
        use_light_color=use_light_color,
        base=base,
    )
    return config


def _controls_platforms(entities) -> set[str]:
    """The entity platforms these controls need compiled into the build."""
    return {_CONTROL_PLATFORMS[key] for key in entities}


def autoload_stub(schema, platform: str):
    """Lets one of the four platform packages recognise an auto-load stub.

    `controls:` asks ESPHome for the platforms its entities need, and ESPHome
    loads a platform by adding an entry to that domain carrying nothing but
    `platform: wled_fx`. The entry creates no entity: it is there so this
    package's sources reach the build. An entry somebody wrote always has a
    `type:`, so an empty one can only be the stub, and it is only accepted when
    AUTO_LOAD says it asked for that platform.
    """

    def validate(config):
        if not config and f"{platform}.{DOMAIN}" in CORE.data.get(_AUTOLOADED_KEY, ()):
            return config
        return schema(config)

    return validate


def is_autoload_stub(config) -> bool:
    """True for the entry AUTO_LOAD added to pull a platform's sources in.

    The schema sees it with `platform:` taken off, so it is empty there and it
    is not here. Every real entry of all four platforms names a `type:`.
    """
    return CONF_TYPE not in config


async def controls_to_code(parent, entities):
    """Builds the named entities and points every one of them at the engine.

    They are the same classes the generic platforms build, so the runtime is
    the one that already exists: each entity publishes what the engine holds at
    setup and subscribes to the controller's state change callback. Nothing here
    adds anything to a frame.
    """
    for key, conf in entities.items():
        if key in SLIDER_KEYS:
            var = cg.new_Pvariable(conf[CONF_ID], NUMBER_TYPES[key])
            await number_component.register_number(
                var, conf, min_value=0, max_value=SLIDER_MAXIMUM[key], step=1
            )
            await cg.register_component(var, conf)
            await cg.register_parented(var, parent)
            if conf[CONF_RESTORE_VALUE]:
                cg.add(var.set_restore_value(True))
        elif key in CHECK_KEYS:
            var = cg.new_Pvariable(conf[CONF_ID], SWITCH_TYPES[key])
            await switch_component.register_switch(var, conf)
            await cg.register_component(var, conf)
            await cg.register_parented(var, parent)
        elif key == PALETTE_KEY:
            var = cg.new_Pvariable(conf[CONF_ID], SELECT_TYPES[CONF_PALETTE])
            await select_component.register_select(var, conf, options=[])
            await cg.register_component(var, conf)
            await cg.register_parented(var, parent)
            if conf[CONF_RESTORE_VALUE]:
                cg.add(var.set_restore_value(True))
        else:
            await color_light_to_code(conf, key, parent)


# --- audio -----------------------------------------------------------------
#
# The analysis source is process wide: the engine has one canvas and one
# segment, so one microphone is enough, and both front ends read it through
# Segment::audio() with no wiring of their own. That is why `audio:` sits on a
# wled_fx entry rather than on each front end, and why an entry that carries
# nothing but `audio:` is a perfectly good way to give the light effect a
# microphone.

# Matches the AgcPreset and FftScaling enums in wf_audio_core.h.
AGC_MODES = {"off": 0, "normal": 1, "vivid": 2, "lazy": 3}
FFT_SCALING_MODES = {"none": 0, "log": 1, "linear": 2, "sqrt": 3}

AUDIO_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_AUDIO_ID): cv.declare_id(WledFxAudioSource),
            cv.Required(CONF_MICROPHONE): microphone.microphone_source_schema(
                min_bits_per_sample=16,
                max_bits_per_sample=16,
                min_channels=1,
                max_channels=1,
            ),
            cv.Optional(CONF_PASSIVE, default=False): cv.boolean,
            cv.Optional(CONF_GAIN, default=60): cv.int_range(min=0, max=255),
            cv.Optional(CONF_SQUELCH, default=10): cv.int_range(min=0, max=255),
            cv.Optional(CONF_INPUT_LEVEL, default=128): cv.int_range(min=0, max=255),
            cv.Optional(CONF_AGC, default="normal"): cv.enum(AGC_MODES, lower=True),
            cv.Optional(CONF_SCALING, default="sqrt"): cv.enum(
                FFT_SCALING_MODES, lower=True
            ),
            cv.Optional(CONF_LIMITER, default=True): cv.boolean,
            cv.Optional(CONF_ATTACK, default="80ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(milliseconds=10000)),
            ),
            cv.Optional(CONF_DECAY, default="1400ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(milliseconds=10000)),
            ),
            cv.Optional(CONF_MIC_FILTER, default=False): cv.boolean,
            cv.Optional(CONF_BANDPASS, default=False): cv.boolean,
            cv.Optional(CONF_TASK_IN_PSRAM, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    # The microphone component, and therefore this block, is ESP32 only.
    cv.only_on([PLATFORM_ESP32]),
)


_ENTRY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WledFxDisplay),
        cv.Optional(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Optional(CONF_EFFECTS): cv.ensure_list(_known_effect),
        cv.Optional(CONF_WIDTH): cv.positive_not_null_int,
        cv.Optional(CONF_HEIGHT): cv.positive_not_null_int,
        # No defaults on these two: _validate_entry() has to be able to tell
        # "not given" from "given", because an entry with no display_id never
        # becomes a component and would drop them silently. The defaults are
        # applied in to_code() instead.
        #
        # gamma_correct is an exponent, and 0.0 makes pow(i / 255, 0) == 1 for
        # every input, so the whole panel goes to full white and stays there.
        # The default, applied in to_code(), is WLED's 2.2.
        cv.Optional(CONF_GAMMA_CORRECT): cv.float_range(min=0.1, max=10.0),
        # A display is a 2D output, so this is the opt-in that puts the 1D-only
        # effects back on the list. No default, for the same reason as the two
        # above: an entry with no display never becomes a component.
        cv.Optional(CONF_INCLUDE_1D_EFFECTS): cv.boolean,
        cv.Optional(CONF_AUDIO): AUDIO_SCHEMA,
        cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CONTROLS): _controls_option,
        **CONTROL_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


def _reject_esp8266(config):
    """ESP8266 is not a supported target, so say so rather than half support it.

    Nothing here has been built or run on one. WLED's own ESP8266 build gives an
    effect a scratch budget of 384 bytes against the 4096 an ESP32-S3 gets, the
    engine keeps a full RGB canvas of its own on top of whatever the output
    needs, and 223 effect bodies is a lot of flash for a 1 MB part. A branch
    nobody can test is worse than an honest refusal.
    """
    if CORE.is_esp8266:
        raise cv.Invalid(
            "wled_fx does not support the ESP8266. The engine has only ever been "
            "built and measured on ESP32 family boards and on the host platform, "
            "and an ESP8266 has neither the RAM the effect scratch budget assumes "
            "nor the flash for the effect list."
        )
    return config


def _validate_entry(config):
    _reject_esp8266(config)
    if CONF_DISPLAY_ID in config:
        # A display is always a 2D output: that is what makes it a display.
        _check_effect_fits(config, True, config.get(CONF_INCLUDE_1D_EFFECTS, False))
        # Colour 1 on a display is also the master brightness and the panel's
        # on/off, and the display front end owns its colours outright.
        return _expand_controls(config, display=True, use_light_color=False)
    # An entry with no display never becomes a component, so anything that would
    # be applied to one would be silently dropped. Say so instead.
    for key in (
        CONF_WIDTH,
        CONF_HEIGHT,
        CONF_GAMMA_CORRECT,
        CONF_INCLUDE_1D_EFFECTS,
        CONF_UPDATE_INTERVAL,
        CONF_CONTROLS,
        *_CONTROL_KEYS,
    ):
        if key in config:
            raise cv.Invalid(
                f"'{key}' only applies to a wled_fx entry that drives a display. "
                "The light front end takes the same keys under the 'wled_fx' "
                "effect in the light's 'effects:' list.",
                path=[key],
            )
    return config


CONFIG_SCHEMA = cv.ensure_list(cv.All(_ENTRY_SCHEMA, _validate_entry))


# Where _final_validate() leaves what it worked out for to_code().
_LAYOUT_DATA_KEY = "wled_fx_layouts"
_ALLOW_LIST_KEY = "wled_fx_allow_list"
_SELECTION_DONE_KEY = "wled_fx_selection_emitted"
# Which entity platforms a light effect's `controls:` needs. The light effect
# is validated before AUTO_LOAD runs, and AUTO_LOAD is only handed the
# `wled_fx:` config, so the light side leaves the answer here.
_LIGHT_PLATFORMS_KEY = "wled_fx_light_control_platforms"
# What AUTO_LOAD asked for, so autoload_stub() can tell a stub from an entry
# somebody wrote and left unfinished.
_AUTOLOADED_KEY = "wled_fx_autoloaded_platforms"


def AUTO_LOAD(config):
    """The entity platforms `controls:` needs, and only when it needs them.

    ESPHome copies a component's sources only for the components a
    configuration actually loads, and only defines USE_NUMBER and its
    neighbours once an entity of that kind exists. So a build with no
    `controls:` pays nothing for this, and a build with it gets exactly the
    platforms its pinned effect turned out to need. ESPHome loads a platform by
    adding an entry carrying nothing but `platform:`, which each of the four
    platform packages recognises and skips.

    Dynamic AUTO_LOAD runs after every component's schema validation, which is
    why the light effect can leave its answer in CORE.data first.
    """
    needed: set[str] = set(CORE.data.get(_LIGHT_PLATFORMS_KEY, set()))
    for item in config or []:
        for entry in item if isinstance(item, list) else [item]:
            if entities := entry.get(CONF_CONTROLS):
                needed |= _controls_platforms(entities)
    CORE.data[_AUTOLOADED_KEY] = needed
    return sorted(needed)


def _all_entries(full_config):
    """Every wled_fx entry in the configuration, whichever way it arrives.

    MULTI_CONF makes ESPHome split `wled_fx:` and hand each entry to to_code()
    and to final validation on its own, and because CONFIG_SCHEMA is an
    ensure_list each of those arrives wrapped in a list of one. Anything that
    has to see all of them, which is everything about the allow-list and the
    layouts, has to come back here for them rather than trust what it was
    passed.
    """
    entries = []
    for item in full_config.get(DOMAIN, []) or []:
        if isinstance(item, list):
            entries.extend(item)
        elif isinstance(item, dict):
            entries.append(item)
    return entries


def _configured_light_effects(full_config):
    """Every wled_fx entry in a light's 'effects:' list, wherever it is."""
    for light_config in full_config.get("light", []) or []:
        for effect in light_config.get(CONF_EFFECTS, []) or []:
            if isinstance(effect, dict) and DOMAIN in effect:
                yield effect[DOMAIN]


def _configured_layouts(entries, full_config):
    """(two_dimensional, include_1d) for every front end in the configuration."""
    layouts = [
        (True, entry.get(CONF_INCLUDE_1D_EFFECTS, False))
        for entry in entries
        if CONF_DISPLAY_ID in entry
    ]
    layouts.extend(
        (_light_effect_is_2d(effect), effect.get(CONF_INCLUDE_1D_EFFECTS, False))
        for effect in _configured_light_effects(full_config)
    )
    return layouts


def _validate_allow_list(entries, offered: set[str] | None, layouts):
    """An 'effects:' name no configured output could ever show is an error.

    Compiling it in would cost flash for something nothing can select. The
    message has to say which way out applies, and that depends on what the
    configuration actually has.
    """
    if offered is None:
        return
    folded = {name.casefold() for name in offered}
    only_2d_outputs = layouts and all(two_d for two_d, _ in layouts)
    for entry in entries:
        for index, name in enumerate(entry.get(CONF_EFFECTS, [])):
            if name.casefold() in folded:
                continue
            if only_2d_outputs:
                reason = (
                    f'"{name}" is a 1D effect and every output in this '
                    f"configuration is a matrix, so nothing could select it. "
                    f"{_OPT_IN_LINE}"
                )
            else:
                reason = (
                    f'"{name}" only runs on a matrix and no output in this '
                    "configuration is one, so nothing could select it. "
                    f"{_GIVE_GEOMETRY_LINE}"
                )
            raise cv.Invalid(reason, path=[CONF_EFFECTS, index])


def _front_end_configs(entries, full_config):
    """Every configuration in the file that owns a canvas and an engine."""
    out = [entry for entry in entries if CONF_DISPLAY_ID in entry]
    out.extend(_configured_light_effects(full_config))
    return out


# The two ways out, written once so every message about the choice says the
# same thing.
_GENERIC_CONTROLS_LINE = (
    "the generic 'number', 'switch' and 'select' platforms with the 'controls' "
    "text sensor, which names them for whichever effect is running"
)


def _check_named_controls(full_config, entries, allow_list):
    """Everything about `controls:` that needs the whole configuration in view.

    Returns the allow-list to build with, which for a pinned single effect is
    that one effect: nothing else could ever be selected, so nothing else is
    worth the flash.
    """
    front_ends = _front_end_configs(entries, full_config)
    pinned = [config for config in front_ends if config.get(CONF_CONTROLS)]
    if not pinned:
        return allow_list

    # A generic entity for a controller that already has named ones is two
    # entities for one control, one of them called Custom 1.
    targets = {
        config[CONF_ID].id: config[CONF_EFFECT]
        for config in pinned
        if config.get(CONF_ID) is not None and config[CONF_ID].id
    }
    for domain in ("number", "switch", "select", "light"):
        for index, item in enumerate(full_config.get(domain, []) or []):
            if not isinstance(item, dict) or item.get(CONF_PLATFORM) != DOMAIN:
                continue
            target = item.get(CONF_WLED_FX_ID)
            if target is None or target.id not in targets:
                continue
            raise cv.Invalid(
                f"'{CONF_CONTROLS}' on the wled_fx entry '{target.id}' already "
                f"builds a named entity for every control "
                f'"{targets[target.id]}" uses, and this \'{domain}\' entry adds '
                "a generic one for the same controller. Pick one: named "
                "controls for a single pinned effect, or "
                f"{_GENERIC_CONTROLS_LINE}. Remove this '{domain}' entry, or "
                f"remove '{CONF_CONTROLS}'.",
                path=[domain, index],
            )

    names = sorted({config[CONF_EFFECT] for config in pinned})
    if allow_list:
        if {name.casefold() for name in allow_list} != {
            name.casefold() for name in names
        }:
            raise cv.Invalid(
                f"'{CONF_CONTROLS}' names the entities after "
                f"{_and_list(names)}, and this '{CONF_EFFECTS}' allow-list "
                f"names {_and_list(sorted(allow_list))}. The named entities "
                "would be the wrong ones for anything else that got selected. "
                f"Either cut the allow-list down to {_and_list(names)}, which "
                "is what leaving it out does on its own, or drop "
                f"'{CONF_CONTROLS}' and use {_GENERIC_CONTROLS_LINE}.",
                path=[DOMAIN, 0, CONF_EFFECTS],
            )
    elif len(pinned) == len(front_ends):
        # Every output in the build is pinned, so nothing can ever select
        # anything else and the other 222 effects are flash spent on nothing.
        allow_list = names
    return allow_list


def _final_validate(config):
    """A display handed to wled_fx must not also be driven by its own poller."""
    full_config = fv.full_config.get()

    # Every entry, not only the one this call was handed: the allow-list is
    # merged across all of them and the layouts are the union of all of them.
    entries = _all_entries(full_config)

    # One canvas, one segment, one analysis source. This cannot live in
    # CONFIG_SCHEMA, because MULTI_CONF hands that one entry at a time and it
    # would never see the second block.
    if sum(1 for entry in entries if CONF_AUDIO in entry) > 1:
        raise cv.Invalid(
            "Only one wled_fx entry can carry an 'audio' block. "
            "Every effect reads the same analysis source."
        )

    layouts = _configured_layouts(entries, full_config)
    if layouts:
        offered: set[str] | None = set()
        for two_dimensional, include_1d in layouts:
            offered |= available_effects(two_dimensional, include_1d)
    else:
        # No front end at all, which is what examples/host.yaml is: a build that
        # only proves the engine compiles. Nothing can be ruled out, so nothing
        # is, and every effect stays available.
        offered = None
    _validate_allow_list(entries, offered, layouts)
    CORE.data[_LAYOUT_DATA_KEY] = offered
    allow_list: list[str] = []
    for entry in entries:
        allow_list.extend(entry.get(CONF_EFFECTS, []))
    CORE.data[_ALLOW_LIST_KEY] = _check_named_controls(
        full_config, entries, allow_list
    )

    for entry in config:
        if (audio_config := entry.get(CONF_AUDIO)) is not None:
            # Checks the microphone really offers the channel that was asked for.
            microphone.final_validate_microphone_source_schema("wled_fx")(
                audio_config[CONF_MICROPHONE]
            )
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
        _warn_on_double_gamma(entry, display_config)
    return config


def _warn_on_double_gamma(entry, display_config):
    """Two curves in a row is a panel much darker than WLED, so say so.

    WLED's HUB75 builds compile the panel library with -D NO_CIE1931
    (WLED platformio.ini, the shared [hub75] flags), so the only curve on a
    WLED panel is the gamma 2.2 that show() applies. ESPHome's hub75 driver
    applies CIE1931 unless it is told otherwise
    (esp-hub75 include/hub75_config.h, HUB75_GAMMA_MODE 1), and that lands on
    top of this component's output gamma. A warning rather than an error: the
    combination is a look, not a mistake, and only the user knows which they
    want.
    """
    if display_config.get(CONF_PLATFORM) != "hub75":
        return
    gamma = entry.get(CONF_GAMMA_CORRECT, DEFAULT_OUTPUT_GAMMA)
    driver_curve = str(display_config.get(CONF_GAMMA_CORRECT, "CIE1931"))
    if gamma == 1.0 or driver_curve == "LINEAR":
        return
    _LOGGER.warning(
        "wled_fx applies gamma %.2f and the hub75 display applies %s on top of it, "
        "so this panel will be darker than a WLED device. A WLED HUB75 build "
        "disables the driver curve: set 'gamma_correct: LINEAR' on the hub75 "
        "display to match it, or 'gamma_correct: 1.0' on wled_fx to leave the "
        "curve to the driver.",
        gamma,
        driver_curve,
    )


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


def _add_effect_selection():
    """Emits the compile-time allow-list and forces the effect groups to link.

    ESPHome builds the generated sources into a static library, so a translation
    unit nothing refers to is never pulled out of the archive and its registration
    object never runs. Naming the group symbols from the generated main.cpp is what
    keeps them in.

    Exactly once, however many wled_fx entries there are. MULTI_CONF calls
    to_code() once per entry, and emitting the group table twice is a
    redefinition the compiler rejects, which is what made two entries
    unbuildable however they were written.
    """
    if CORE.data.get(_SELECTION_DONE_KEY):
        return
    CORE.data[_SELECTION_DONE_KEY] = True

    # The lists of every entry merged into one, worked out in final validation
    # because that is the only place all the entries are in view at once.
    selected: list[str] = list(CORE.data.get(_ALLOW_LIST_KEY, []))

    # With no allow-list the build carries every effect the configured outputs
    # could offer, which on a matrix-only configuration is not every effect: a
    # 1D-only effect nothing can select is flash spent on nothing. Final
    # validation worked the set out, because it is the only place the light
    # effects and the display front ends are both in view.
    offered = CORE.data.get(_LAYOUT_DATA_KEY)
    compiled_in = selected
    if not compiled_in and offered is not None:
        compiled_in = sorted(offered)

    macros = {effect_macro(name) for name in compiled_in}
    # Nothing narrowed it down, so let the sources take their own default rather
    # than list 223 macros on the compiler command line.
    all_effects = not macros or len(macros) == len(effect_names())

    if all_effects:
        cg.add_build_flag("-DWLED_FX_ALL_EFFECTS")
        macros = set()
    else:
        for macro in sorted(macros):
            cg.add_build_flag(f"-D{macro}=1")

    groups = _discover_effect_groups()
    provided = set().union(*groups.values()) if groups else set()
    # The name was checked against the registry, so a macro with no group behind
    # it means a group guard line in a wf_effects_*.cpp forgot to list it, and
    # that effect would silently not be compiled in.
    missing = sorted(macros - provided)
    if missing:
        raise cv.Invalid(
            "These effects are registered but no effect group's "
            f"WLED_FX_GROUP_* guard names them, so an allow-list cannot pull "
            f"them in: {', '.join(missing)}. This is a bug in the component."
        )

    linked = [
        symbol
        for symbol, provides in sorted(groups.items())
        if all_effects or (provides & macros)
    ]
    if not linked:
        raise cv.Invalid(
            "No effect group was selected, so no effect would be compiled in."
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


async def audio_to_code(config):
    """Builds the real analysis source and pulls in the FFT dependency.

    esp-dsp is already pinned by ESPHome itself, so asking for it here costs no
    new third party dependency. On Arduino this replaces the empty stub ESPHome
    substitutes for the component; on esp-idf it adds it.

    WLED_FX_AUDIO is what compiles the microphone plumbing in at all, and
    WLED_FX_USE_ESP_DSP picks the esp-dsp FFT. Neither is defined in a build
    without an audio block, so those sources come out empty and wf_fft.cpp keeps
    its own radix-2 transform, which is also the one the host simulator runs.
    """
    from esphome.components import esp32

    esp32.add_idf_component(name="espressif/esp-dsp", ref="1.8.2")
    cg.add_build_flag("-DWLED_FX_AUDIO")
    cg.add_build_flag("-DWLED_FX_USE_ESP_DSP")

    var = cg.new_Pvariable(config[CONF_AUDIO_ID])
    await cg.register_component(var, config)

    mic_source = await microphone.microphone_source_to_code(
        config[CONF_MICROPHONE], passive=config[CONF_PASSIVE]
    )
    cg.add(var.set_microphone_source(mic_source))

    cg.add(var.set_gain(config[CONF_GAIN]))
    cg.add(var.set_squelch(config[CONF_SQUELCH]))
    cg.add(var.set_input_level(config[CONF_INPUT_LEVEL]))
    cg.add(var.set_agc(config[CONF_AGC]))
    cg.add(var.set_scaling(config[CONF_SCALING]))
    cg.add(var.set_limiter(config[CONF_LIMITER]))
    cg.add(var.set_attack(config[CONF_ATTACK].total_milliseconds))
    cg.add(var.set_decay(config[CONF_DECAY].total_milliseconds))
    cg.add(var.set_mic_filter(config[CONF_MIC_FILTER]))
    cg.add(var.set_bandpass(config[CONF_BANDPASS]))
    cg.add(var.set_task_in_psram(config[CONF_TASK_IN_PSRAM]))
    return var


async def to_code(config):
    _add_effect_selection()
    for entry in config:
        if (audio_config := entry.get(CONF_AUDIO)) is not None:
            await audio_to_code(audio_config)
        if CONF_DISPLAY_ID not in entry:
            continue
        var = cg.new_Pvariable(entry[CONF_ID])
        # register_component() emits set_update_interval() for any config that
        # carries the key, and this is a plain Component with its own frame
        # clock, not a PollingComponent. The key keeps the name a reader of the
        # light effect would expect; it just does not go through there.
        await cg.register_component(
            var, {k: v for k, v in entry.items() if k != CONF_UPDATE_INTERVAL}
        )
        target = await cg.get_variable(entry[CONF_DISPLAY_ID])
        cg.add(var.set_display(target))
        cg.add(
            var.set_dimensions(entry.get(CONF_WIDTH, 0), entry.get(CONF_HEIGHT, 0))
        )
        cg.add(var.set_gamma(entry.get(CONF_GAMMA_CORRECT, DEFAULT_OUTPUT_GAMMA)))
        # A display is a matrix, so this front end is always a 2D output.
        cg.add(var.set_layout_2d(True))
        cg.add(
            var.set_include_1d_effects(entry.get(CONF_INCLUDE_1D_EFFECTS, False))
        )
        cg.add(
            var.set_frame_interval(
                entry.get(CONF_UPDATE_INTERVAL, DEFAULT_FRAME_INTERVAL)
            )
        )
        await apply_controls(var, entry)
        await controls_to_code(var, entry.get(CONF_CONTROLS) or {})


LIGHT_EFFECT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ID): cv.declare_id(WledFxLightEffect),
        cv.Optional(CONF_WIDTH): cv.positive_not_null_int,
        cv.Optional(CONF_HEIGHT): cv.positive_not_null_int,
        cv.Optional(CONF_SERPENTINE, default=False): cv.boolean,
        cv.Optional(CONF_USE_LIGHT_COLOR, default=True): cv.boolean,
        cv.Optional(CONF_INCLUDE_1D_EFFECTS, default=False): cv.boolean,
        cv.Optional(
            CONF_UPDATE_INTERVAL, default=FRAMETIME
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CONTROLS): _controls_option,
        **CONTROL_SCHEMA,
    }
)


def _light_effect_is_2d(config) -> bool:
    """True when this light effect describes a matrix wired as one strip.

    Width on its own leaves start() to compute height = led_count / width, which
    for the usual "width is the whole strip" case is exactly 1, so it takes both
    keys to make a matrix.
    """
    height = config.get(CONF_HEIGHT)
    return height is not None and height > 1


def _validate_light_effect(config):
    """Rejects an effect the light's own geometry cannot run."""
    _reject_esp8266(config)
    two_dimensional = _light_effect_is_2d(config)
    include_1d = config.get(CONF_INCLUDE_1D_EFFECTS, False)
    if include_1d and not two_dimensional:
        raise cv.Invalid(
            f"'{CONF_INCLUDE_1D_EFFECTS}' only means something on a matrix, "
            "where the 1D effects are hidden by default. This light effect is "
            "already a one dimensional strip, so it offers them all. Remove "
            "the option, or give the effect a 'width' and a 'height'.",
            path=[CONF_INCLUDE_1D_EFFECTS],
        )
    _check_effect_fits(config, two_dimensional, include_1d)
    _expand_controls(
        config, display=False, use_light_color=config[CONF_USE_LIGHT_COLOR]
    )
    if entities := config.get(CONF_CONTROLS):
        # AUTO_LOAD only sees the `wled_fx:` config, and this is a light effect,
        # so the platforms this needs are left where AUTO_LOAD will find them.
        platforms = CORE.data.setdefault(_LIGHT_PLATFORMS_KEY, set())
        platforms |= _controls_platforms(entities)
    return config


# --- actions -----------------------------------------------------------------

_PARENT_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(WledFxController)})


@automation.register_action(
    "wled_fx.set_effect",
    SetEffectAction,
    _PARENT_SCHEMA.extend({cv.Required(CONF_EFFECT): cv.templatable(_known_effect)}),
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
    _PARENT_SCHEMA.extend({cv.Required(CONF_PALETTE): cv.templatable(_known_palette)}),
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


CONF_COLOR = "color"
_CHANNEL = cv.templatable(cv.int_range(min=0, max=255))


@automation.register_action(
    "wled_fx.set_color",
    SetColorAction,
    _PARENT_SCHEMA.extend(
        {
            cv.Required(CONF_COLOR): cv.int_range(min=1, max=3),
            cv.Optional(CONF_RED, default=0): _CHANNEL,
            cv.Optional(CONF_GREEN, default=0): _CHANNEL,
            cv.Optional(CONF_BLUE, default=0): _CHANNEL,
        }
    ),
    synchronous=True,
)
async def set_color_action_to_code(config, action_id, template_arg, args):
    # Colour 1 to 3 in the YAML, slot 0 to 2 in the segment.
    var = cg.new_Pvariable(action_id, template_arg, config[CONF_COLOR] - 1)
    await cg.register_parented(var, config[CONF_ID])
    for key, setter in (
        (CONF_RED, var.set_red),
        (CONF_GREEN, var.set_green),
        (CONF_BLUE, var.set_blue),
    ):
        cg.add(setter(await cg.templatable(config[key], args, cg.int_)))
    return var


# --- light effect ---------------------------------------------------------------


@register_addressable_effect(
    "wled_fx",
    WledFxLightEffect,
    "WLED FX",
    LIGHT_EFFECT_SCHEMA,
    _validate_light_effect,
)
async def wled_fx_light_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(config.get(CONF_ID, effect_id), config[CONF_NAME])
    cg.add(var.set_dimensions(config.get(CONF_WIDTH, 0), config.get(CONF_HEIGHT, 0)))
    cg.add(var.set_serpentine(config[CONF_SERPENTINE]))
    cg.add(var.set_use_light_color(config[CONF_USE_LIGHT_COLOR]))
    cg.add(var.set_layout_2d(_light_effect_is_2d(config)))
    cg.add(var.set_include_1d_effects(config[CONF_INCLUDE_1D_EFFECTS]))
    cg.add(var.set_frame_interval(config[CONF_UPDATE_INTERVAL]))
    await apply_controls(var, config)
    await controls_to_code(var, config.get(CONF_CONTROLS) or {})
    return var
