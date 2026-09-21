import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from .. import CONF_WLED_FX_ID, WledFxController, wled_fx_ns

DEPENDENCIES = ["wled_fx"]

WledFxSelect = wled_fx_ns.class_("WledFxSelect", select.Select, cg.Component)
WledFxSelectType = wled_fx_ns.enum("WledFxSelectType", is_class=True)
WledFxSelectScope = wled_fx_ns.enum("WledFxSelectScope", is_class=True)

CONF_SCOPE = "scope"

TYPES = {
    "effect": WledFxSelectType.WLED_FX_SELECT_TYPE_EFFECT,
    "palette": WledFxSelectType.WLED_FX_SELECT_TYPE_PALETTE,
}

# Which slice of the offered effects the dropdown lists. WledFxSelectScope in
# wled_fx_select.h says why the other two exist.
#
#   all     everything this output offers, which is the whole point on a strip
#           and on a matrix that did not set include_1d_effects
#   panel   what an untouched 2D output offers, the genuinely 2D effects
#   strip   what a 1D output offers, which on a matrix are the ones drawn
#           through WLED's 1D to 2D mapping
SCOPES = {
    "all": WledFxSelectScope.WLED_FX_SELECT_SCOPE_ALL,
    "panel": WledFxSelectScope.WLED_FX_SELECT_SCOPE_PANEL,
    "strip": WledFxSelectScope.WLED_FX_SELECT_SCOPE_STRIP,
}


def _validate(config):
    # cv.enum validates to the spelling from the YAML, carrying the C++ symbol
    # alongside it, so this compares the word and codegen emits the symbol.
    if config[CONF_TYPE] == "palette" and CONF_SCOPE in config:
        raise cv.Invalid(
            f"'{CONF_SCOPE}' narrows a list of effects, and this select lists "
            "palettes. Every palette works on every output.",
            path=[CONF_SCOPE],
        )
    return config


CONFIG_SCHEMA = cv.All(
    select.select_schema(WledFxSelect)
    .extend(
        {
            cv.GenerateID(CONF_WLED_FX_ID): cv.use_id(WledFxController),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
            cv.Optional(CONF_SCOPE): cv.enum(SCOPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[select.CONF_ID], config[CONF_TYPE])
    if CONF_SCOPE in config:
        cg.add(var.set_scope(config[CONF_SCOPE]))
    await select.register_select(var, config, options=[])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WLED_FX_ID])
