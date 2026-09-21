"""The three WLED colour slots, one ESPHome light each.

ESPHome has no standalone colour entity, and a light is the only thing that
web_server v3 and Home Assistant both render as a colour picker, so a light is
what a colour slot has to be. See the class comment in wled_fx_color_light.h
for what each of the three does.
"""

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_BLUE,
    CONF_BRIGHTNESS,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_GREEN,
    CONF_INITIAL_STATE,
    CONF_OUTPUT_ID,
    CONF_RED,
    CONF_STATE,
    CONF_TYPE,
)

from .. import CONF_WLED_FX_ID, WledFxController, wled_fx_ns

DEPENDENCIES = ["wled_fx"]

WledFxColorLight = wled_fx_ns.class_("WledFxColorLight", light.LightOutput, cg.Component)
WledFxColorLightType = wled_fx_ns.enum("WledFxColorLightType", is_class=True)

TYPES = {
    "color1": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR1,
    "color2": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR2,
    "color3": WledFxColorLightType.WLED_FX_COLOR_LIGHT_TYPE_COLOR3,
}

# Segment::colors[0] in wf_segment.h, which is WLED's own DEFAULT_COLOR
# (wled00/FX.h:45): 0xFFA000, an amber with 160 of green. It has to be this
# value and not a rounder one, because the light writes it into the engine at
# boot and it is what every effect draws on palette "Default", and what the
# four dynamic palettes are built from.
DEFAULT_COLOR1 = (0xFF, 0xA0, 0x00)


def _schema(restore_mode, initial_state):
    return (
        light.light_schema(
            WledFxColorLight, light.LightType.RGB, default_restore_mode=restore_mode
        )
        .extend(
            {
                cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
                # A colour slot is a value, not a lamp: the picker should hand
                # the engine the colour it shows, without a display gamma bent
                # into it and without a second of fading on the way.
                cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
                cv.Optional(
                    CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
                ): cv.positive_time_period_milliseconds,
                cv.Optional(CONF_INITIAL_STATE, default=initial_state): (
                    light.LIGHT_STATE_SCHEMA
                ),
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
    )


# A fresh device comes up with exactly the colours the engine already had, so
# adding these entities does not change what any effect looks like: colour 1 on
# at WLED's own primary, colour 2 and colour 3 black.
CONFIG_SCHEMA = cv.typed_schema(
    {
        "color1": _schema(
            "RESTORE_DEFAULT_ON",
            {
                CONF_STATE: True,
                CONF_BRIGHTNESS: 1.0,
                CONF_RED: DEFAULT_COLOR1[0] / 255,
                CONF_GREEN: DEFAULT_COLOR1[1] / 255,
                CONF_BLUE: DEFAULT_COLOR1[2] / 255,
            },
        ),
        "color2": _schema("RESTORE_DEFAULT_OFF", {CONF_STATE: False}),
        "color3": _schema("RESTORE_DEFAULT_OFF", {CONF_STATE: False}),
    },
    key=CONF_TYPE,
    lower=True,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], TYPES[config[CONF_TYPE]])
    await light.register_light(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
