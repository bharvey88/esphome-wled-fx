import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE

from .. import (
    CONF_CHECK1,
    CONF_CHECK2,
    CONF_CHECK3,
    CONF_WLED_FX_ID,
    WledFxController,
    wled_fx_ns,
)

DEPENDENCIES = ["wled_fx"]

WledFxSwitch = wled_fx_ns.class_("WledFxSwitch", switch.Switch, cg.Component)
WledFxSwitchType = wled_fx_ns.enum("WledFxSwitchType", is_class=True)

TYPES = {
    CONF_CHECK1: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK1,
    CONF_CHECK2: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK2,
    CONF_CHECK3: WledFxSwitchType.WLED_FX_SWITCH_TYPE_CHECK3,
}

CONFIG_SCHEMA = (
    # DISABLED by default, because the engine owns this checkmark: it comes from
    # YAML if it was pinned there and from the effect's own metadata otherwise,
    # and it is refilled every time the effect changes. Anything else is honoured
    # at boot and then behaves like any other switch, but it is an explicit
    # choice rather than something a default quietly does.
    switch.switch_schema(WledFxSwitch, default_restore_mode="DISABLED")
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
    await switch.register_switch(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
