import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

import esphome.final_validate as fv

CODEOWNERS = ["@gongloo"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

outequip_ac_ns = cg.esphome_ns.namespace("outequip_ac")
OutEquipAC = outequip_ac_ns.class_("OutEquipAC", cg.Component, uart.UARTDevice)

CONF_OUTEQUIP_AC_ID = "outequip_ac_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OutEquipAC),
}).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)

def final_validate(config):
    full_config = fv.full_config.get()
    if "web_server" in full_config:
        web_server_config = full_config["web_server"]
        if isinstance(web_server_config, list):
            for ws in web_server_config:
                ws["log"] = False
        elif isinstance(web_server_config, dict):
            web_server_config["log"] = False
    return config

FINAL_VALIDATE_SCHEMA = final_validate

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)


