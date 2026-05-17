import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, UNIT_VOLT, UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, DEVICE_CLASS_CURRENT, UNIT_AMPERE
from . import outequip_ac_ns, OutEquipAC, CONF_OUTEQUIP_AC_ID

DEPENDENCIES = ["outequip_ac"]

CONF_INTAKE_TEMP = "intake_temp"
CONF_OUTLET_TEMP = "outlet_temp"
CONF_VOLTAGE = "voltage"
CONF_UNDERVOLT = "undervolt"
CONF_OVERVOLT = "overvolt"
CONF_AMPERAGE = "amperage"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_OUTEQUIP_AC_ID): cv.use_id(OutEquipAC),
    cv.Optional(CONF_INTAKE_TEMP): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:import",
    ),
    cv.Optional(CONF_OUTLET_TEMP): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:export",
    ),
    cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:flash",
    ),
    cv.Optional(CONF_UNDERVOLT): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:battery-low",
    ),
    cv.Optional(CONF_OVERVOLT): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:battery-alert-variant",
    ),
    cv.Optional(CONF_AMPERAGE): sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:current-dc",
    ),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_OUTEQUIP_AC_ID])
    
    if CONF_INTAKE_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_INTAKE_TEMP])
        cg.add(parent.set_intake_temp_sensor(sens))
        
    if CONF_OUTLET_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_OUTLET_TEMP])
        cg.add(parent.set_outlet_temp_sensor(sens))
        
    if CONF_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE])
        cg.add(parent.set_voltage_sensor(sens))
        
    if CONF_UNDERVOLT in config:
        sens = await sensor.new_sensor(config[CONF_UNDERVOLT])
        cg.add(parent.set_undervolt_sensor(sens))

    if CONF_OVERVOLT in config:
        sens = await sensor.new_sensor(config[CONF_OVERVOLT])
        cg.add(parent.set_overvolt_sensor(sens))

    if CONF_AMPERAGE in config:
        sens = await sensor.new_sensor(config[CONF_AMPERAGE])
        cg.add(parent.set_amperage_sensor(sens))
