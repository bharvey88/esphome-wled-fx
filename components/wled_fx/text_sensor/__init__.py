import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

from .. import CONF_WLED_FX_ID, WledFxController, wled_fx_ns

DEPENDENCIES = ["wled_fx"]

WledFxTextSensor = wled_fx_ns.class_(
    "WledFxTextSensor", text_sensor.TextSensor, cg.Component
)
WledFxTextSensorType = wled_fx_ns.enum("WledFxTextSensorType", is_class=True)

TYPES = {
    "controls": WledFxTextSensorType.WLED_FX_TEXT_SENSOR_TYPE_CONTROLS,
    "colors": WledFxTextSensorType.WLED_FX_TEXT_SENSOR_TYPE_COLORS,
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(
        WledFxTextSensor, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], TYPES[config[CONF_TYPE]])
    await text_sensor.register_text_sensor(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
