import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_VISUAL,
    CONF_TEMPERATURE_STEP,
    CONF_MIN_TEMPERATURE,
    CONF_MAX_TEMPERATURE,
)
from . import outequip_ac_ns, OutEquipAC, CONF_OUTEQUIP_AC_ID

DEPENDENCIES = ["outequip_ac"]

def default_visual_specs(config):
    from esphome.const import CONF_TARGET_TEMPERATURE, CONF_CURRENT_TEMPERATURE
    visual = config.setdefault(CONF_VISUAL, {})
    visual.setdefault(CONF_MIN_TEMPERATURE, 16)
    visual.setdefault(CONF_MAX_TEMPERATURE, 30)
    temp_step = visual.setdefault(CONF_TEMPERATURE_STEP, {})
    if not isinstance(temp_step, dict):
        val = temp_step
        temp_step = visual[CONF_TEMPERATURE_STEP] = {}
        temp_step[CONF_TARGET_TEMPERATURE] = val
        temp_step[CONF_CURRENT_TEMPERATURE] = val
    temp_step.setdefault(CONF_TARGET_TEMPERATURE, 0.1)
    temp_step.setdefault(CONF_CURRENT_TEMPERATURE, 0.1)
    return config

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(OutEquipAC).extend({
        cv.GenerateID(CONF_OUTEQUIP_AC_ID): cv.use_id(OutEquipAC),
        cv.Optional(CONF_ICON, default="mdi:air-conditioner"): cv.icon,
    }).extend(cv.COMPONENT_SCHEMA),
    default_visual_specs
)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_OUTEQUIP_AC_ID])
    await climate.register_climate(parent, config)

