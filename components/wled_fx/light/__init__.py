"""The three WLED colour slots, one ESPHome light each.

ESPHome has no standalone colour entity, and a light is the only thing that
web_server v3 and Home Assistant both render as a colour picker, so a light is
what a colour slot has to be. See the class comment in wled_fx_color_light.h
for what each of the three does.

The schema and the codegen live in the parent package, because `controls:`
builds the same lights from there.
"""

import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from .. import (
    COLOR_LIGHT_TYPES,
    CONF_WLED_FX_ID,
    autoload_stub,
    is_autoload_stub,
    color_light_schema,
    color_light_to_code,
)

DEPENDENCIES = ["wled_fx"]

_ENTRY_SCHEMA = cv.typed_schema(
    {slot: color_light_schema(slot) for slot in COLOR_LIGHT_TYPES},
    key=CONF_TYPE,
    lower=True,
)

CONFIG_SCHEMA = autoload_stub(_ENTRY_SCHEMA, "light")


async def to_code(config):
    if is_autoload_stub(config):
        return
    await color_light_to_code(config, config[CONF_TYPE], config[CONF_WLED_FX_ID])
