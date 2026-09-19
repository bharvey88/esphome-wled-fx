import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE

from .. import (
    CONF_CUSTOM1,
    CONF_CUSTOM2,
    CONF_CUSTOM3,
    CONF_INTENSITY,
    CONF_SPEED,
    CONF_WLED_FX_ID,
    WledFxController,
    wled_fx_ns,
)

DEPENDENCIES = ["wled_fx"]

WledFxNumber = wled_fx_ns.class_("WledFxNumber", number.Number, cg.Component)
WledFxNumberType = wled_fx_ns.enum("WledFxNumberType", is_class=True)

TYPES = {
    CONF_SPEED: (WledFxNumberType.WLED_FX_NUMBER_TYPE_SPEED, 255),
    CONF_INTENSITY: (WledFxNumberType.WLED_FX_NUMBER_TYPE_INTENSITY, 255),
    CONF_CUSTOM1: (WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM1, 255),
    CONF_CUSTOM2: (WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM2, 255),
    CONF_CUSTOM3: (WledFxNumberType.WLED_FX_NUMBER_TYPE_CUSTOM3, 31),
}

CONFIG_SCHEMA = (
    number.number_schema(WledFxNumber)
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    enum_value, maximum = TYPES[config[CONF_TYPE]]
    var = cg.new_Pvariable(config[CONF_ID], enum_value)
    await number.register_number(var, config, min_value=0, max_value=maximum, step=1)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
