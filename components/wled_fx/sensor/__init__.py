import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_TYPE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)

# ESPHome has no microsecond constant, and Home Assistant's "duration" device
# class does not accept one either, so the unit is given and the device class
# left off.
UNIT_MICROSECOND = "µs"

from .. import CONF_WLED_FX_ID, WledFxController, wled_fx_ns

DEPENDENCIES = ["wled_fx"]

WledFxSensor = wled_fx_ns.class_(
    "WledFxSensor", sensor.Sensor, cg.PollingComponent
)
WledFxSensorType = wled_fx_ns.enum("WledFxSensorType", is_class=True)

TYPES = {
    "render_time": WledFxSensorType.WLED_FX_SENSOR_TYPE_RENDER_TIME,
    "output_time": WledFxSensorType.WLED_FX_SENSOR_TYPE_OUTPUT_TIME,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        WledFxSensor,
        unit_of_measurement=UNIT_MICROSECOND,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        }
    )
    .extend(cv.polling_component_schema("5s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], TYPES[config[CONF_TYPE]])
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
