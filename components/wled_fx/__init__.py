"""WLED effect engine for ESPHome.

This file is independent work. It holds no effect list, palette data or parameter
tables copied from WLED: the names it validates against are read out of the C++
sources at config time, and everything derived from WLED lives there, under
GPLv3. See PORTING.md.
"""

from pathlib import Path
import re

from esphome import automation
import esphome.codegen as cg
from esphome.components import display, microphone
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.const import (
    CONF_BLUE,
    CONF_DISPLAY_ID,
    CONF_GAMMA_CORRECT,
    CONF_GREEN,
    CONF_HEIGHT,
    CONF_ID,
    CONF_MICROPHONE,
    CONF_NAME,
    CONF_RED,
    CONF_TEXT,
    CONF_UPDATE_INTERVAL,
    CONF_WIDTH,
    PLATFORM_ESP32,
)
import esphome.final_validate as fv

from .effect_index import (
    available_effects,
    effect_macro,
    effect_names,
    one_dimensional_only,
    palette_names,
    suggestion,
    two_dimensional_only,
)

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
        cv.Optional(CONF_GAMMA_CORRECT): cv.float_range(min=0.1, max=10.0),
        # A display is a 2D output, so this is the opt-in that puts the 1D-only
        # effects back on the list. No default, for the same reason as the two
        # above: an entry with no display never becomes a component.
        cv.Optional(CONF_INCLUDE_1D_EFFECTS): cv.boolean,
        cv.Optional(CONF_AUDIO): AUDIO_SCHEMA,
        cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        **CONTROL_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


def _validate_entry(config):
    if CONF_DISPLAY_ID in config:
        # A display is always a 2D output: that is what makes it a display.
        _check_effect_fits(config, True, config.get(CONF_INCLUDE_1D_EFFECTS, False))
        return config
    # An entry with no display never becomes a component, so anything that would
    # be applied to one would be silently dropped. Say so instead.
    for key in (
        CONF_WIDTH,
        CONF_HEIGHT,
        CONF_GAMMA_CORRECT,
        CONF_INCLUDE_1D_EFFECTS,
        CONF_UPDATE_INTERVAL,
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


def _validate_one_audio_source(config):
    """One canvas, one segment, one analysis source."""
    if sum(1 for entry in config if CONF_AUDIO in entry) > 1:
        raise cv.Invalid(
            "Only one wled_fx entry can carry an 'audio' block. "
            "Every effect reads the same analysis source."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(cv.All(_ENTRY_SCHEMA, _validate_entry)), _validate_one_audio_source
)


# Where _final_validate() leaves what it worked out for to_code(). Final
# validation runs once, before any to_code(), and it is the only place the light
# effects and the wled_fx entries are both in view.
_LAYOUT_DATA_KEY = "wled_fx_layouts"


def _configured_light_effects(full_config):
    """Every wled_fx entry in a light's 'effects:' list, wherever it is."""
    for light_config in full_config.get("light", []) or []:
        for effect in light_config.get(CONF_EFFECTS, []) or []:
            if isinstance(effect, dict) and DOMAIN in effect:
                yield effect[DOMAIN]


def _configured_layouts(config, full_config):
    """(two_dimensional, include_1d) for every front end in the configuration."""
    layouts = [
        (True, entry.get(CONF_INCLUDE_1D_EFFECTS, False))
        for entry in config
        if CONF_DISPLAY_ID in entry
    ]
    layouts.extend(
        (_light_effect_is_2d(effect), effect.get(CONF_INCLUDE_1D_EFFECTS, False))
        for effect in _configured_light_effects(full_config)
    )
    return layouts


def _validate_allow_list(config, offered: set[str] | None, layouts):
    """An 'effects:' name no configured output could ever show is an error.

    Compiling it in would cost flash for something nothing can select. The
    message has to say which way out applies, and that depends on what the
    configuration actually has.
    """
    if offered is None:
        return
    folded = {name.casefold() for name in offered}
    only_2d_outputs = layouts and all(two_d for two_d, _ in layouts)
    for entry in config:
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


def _final_validate(config):
    """A display handed to wled_fx must not also be driven by its own poller."""
    full_config = fv.full_config.get()

    layouts = _configured_layouts(config, full_config)
    if layouts:
        offered: set[str] | None = set()
        for two_dimensional, include_1d in layouts:
            offered |= available_effects(two_dimensional, include_1d)
    else:
        # No front end at all, which is what examples/host.yaml is: a build that
        # only proves the engine compiles. Nothing can be ruled out, so nothing
        # is, and every effect stays available.
        offered = None
    _validate_allow_list(config, offered, layouts)
    CORE.data[_LAYOUT_DATA_KEY] = offered

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
    _add_effect_selection(config)
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
        cg.add(var.set_gamma(entry.get(CONF_GAMMA_CORRECT, 1.0)))
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
    return var
