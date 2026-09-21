import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE

from .. import (
    NUMBER_TYPES,
    CONF_WLED_FX_ID,
    SLIDER_MAXIMUM,
    WledFxController,
    WledFxNumber,
    autoload_stub,
    is_autoload_stub,
)

DEPENDENCIES = ["wled_fx"]

_ENTRY_SCHEMA = (
    number.number_schema(WledFxNumber)
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.one_of(*NUMBER_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = autoload_stub(_ENTRY_SCHEMA, "number")


async def to_code(config):
    if is_autoload_stub(config):
        return
    var = cg.new_Pvariable(config[CONF_ID], NUMBER_TYPES[config[CONF_TYPE]])
    await number.register_number(
        var, config, min_value=0, max_value=SLIDER_MAXIMUM[config[CONF_TYPE]], step=1
    )
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
