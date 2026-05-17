import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID, CONF_ICON
from . import outequip_ac_ns, OutEquipAC, CONF_OUTEQUIP_AC_ID

DEPENDENCIES = ["outequip_ac"]

CONFIG_SCHEMA = climate.climate_schema(OutEquipAC).extend({
    cv.GenerateID(CONF_OUTEQUIP_AC_ID): cv.use_id(OutEquipAC),
    cv.Optional(CONF_ICON, default="mdi:air-conditioner"): cv.icon,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_OUTEQUIP_AC_ID])
    await climate.register_climate(parent, config)
