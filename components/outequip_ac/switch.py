import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_ICON, CONF_RESTORE_MODE
from . import outequip_ac_ns, OutEquipAC, CONF_OUTEQUIP_AC_ID

DEPENDENCIES = ["outequip_ac"]

CONF_LCD = "lcd"
CONF_SWING = "swing"
CONF_LIGHT = "light"

OutEquipACSwitch = outequip_ac_ns.class_("OutEquipACSwitch", switch.Switch, cg.Component)
OutEquipACSwitchType = outequip_ac_ns.enum("OutEquipACSwitchType")

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_OUTEQUIP_AC_ID): cv.use_id(OutEquipAC),
    cv.Optional(CONF_LCD): switch.switch_schema(
        OutEquipACSwitch,
        icon="mdi:clock-digital",
    ).extend(cv.COMPONENT_SCHEMA),
    cv.Optional(CONF_SWING): switch.switch_schema(
        OutEquipACSwitch,
        icon="mdi:swap-horizontal",
    ).extend(cv.COMPONENT_SCHEMA),
    cv.Optional(CONF_LIGHT): switch.switch_schema(
        OutEquipACSwitch,
        icon="mdi:wall-sconce-flat",
    ).extend(cv.COMPONENT_SCHEMA),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_OUTEQUIP_AC_ID])
    
    if CONF_LCD in config:
        conf = config[CONF_LCD]
        var = await switch.new_switch(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_type(OutEquipACSwitchType.LCD))
        cg.add(parent.set_lcd_switch(var))

    if CONF_SWING in config:
        conf = config[CONF_SWING]
        var = await switch.new_switch(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_type(OutEquipACSwitchType.SWING))
        cg.add(parent.set_swing_switch(var))

    if CONF_LIGHT in config:
        conf = config[CONF_LIGHT]
        if CONF_RESTORE_MODE not in conf:
            conf[CONF_RESTORE_MODE] = "RESTORE_DEFAULT_OFF"
        var = await switch.new_switch(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_type(OutEquipACSwitchType.LIGHT))
        cg.add(parent.set_light_switch(var))
