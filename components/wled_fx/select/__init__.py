import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from .. import CONF_WLED_FX_ID, WledFxController, wled_fx_ns

DEPENDENCIES = ["wled_fx"]

WledFxSelect = wled_fx_ns.class_("WledFxSelect", select.Select, cg.Component)
WledFxSelectType = wled_fx_ns.enum("WledFxSelectType", is_class=True)

TYPES = {
    "effect": WledFxSelectType.WLED_FX_SELECT_TYPE_EFFECT,
    "palette": WledFxSelectType.WLED_FX_SELECT_TYPE_PALETTE,
}

CONFIG_SCHEMA = (
    select.select_schema(WledFxSelect)
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[select.CONF_ID], config[CONF_TYPE])
    await select.register_select(var, config, options=[])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
