import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from .. import (
    SELECT_TYPES,
    CONF_WLED_FX_ID,
    WledFxController,
    WledFxSelect,
    autoload_stub,
    is_autoload_stub,
)

DEPENDENCIES = ["wled_fx"]

_ENTRY_SCHEMA = (
    select.select_schema(WledFxSelect)
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.enum(SELECT_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = autoload_stub(_ENTRY_SCHEMA, "select")


async def to_code(config):
    if is_autoload_stub(config):
        return
    var = cg.new_Pvariable(config[select.CONF_ID], config[CONF_TYPE])
    await select.register_select(var, config, options=[])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
