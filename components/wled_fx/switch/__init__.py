import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE

from .. import (
    SWITCH_TYPES,
    CONF_WLED_FX_ID,
    WledFxController,
    WledFxSwitch,
    autoload_stub,
    is_autoload_stub,
)

DEPENDENCIES = ["wled_fx"]

_ENTRY_SCHEMA = (
    # DISABLED by default, because the engine owns this checkmark: it comes from
    # YAML if it was pinned there and from the effect's own metadata otherwise,
    # and it is refilled every time the effect changes. Anything else is honoured
    # at boot and then behaves like any other switch, but it is an explicit
    # choice rather than something a default quietly does.
    switch.switch_schema(WledFxSwitch, default_restore_mode="DISABLED")
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.one_of(*SWITCH_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = autoload_stub(_ENTRY_SCHEMA, "switch")


async def to_code(config):
    if is_autoload_stub(config):
        return
    var = cg.new_Pvariable(config[CONF_ID], SWITCH_TYPES[config[CONF_TYPE]])
    await switch.register_switch(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
