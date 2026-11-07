import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_TYPE, STATE_CLASS_MEASUREMENT, STATE_CLASS_TOTAL_INCREASING, ENTITY_CATEGORY_DIAGNOSTIC
from . import (bus_t4_ns, PARENT_SCHEMA, CONF_BUS_T4_ID, CONF_REGISTER, CONF_BYTES, CONF_MULTIPLIER,
               check_type_or_register)

BusT4Sensor = bus_t4_ns.class_("BusT4Sensor", sensor.Sensor, cg.Component)

# type: (регистр, байт)
TYPES = {
    "maneuvers_count":       (0xB2, 2),   # счётчик манёвров (Partial count)
    "maintenance_threshold": (0xB1, 2),   # порог обслуживания (манёвров)
    "custom":                (None, 1),
}

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(BusT4Sensor, accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT,
                         entity_category=ENTITY_CATEGORY_DIAGNOSTIC).extend(PARENT_SCHEMA).extend({
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_REGISTER): cv.hex_uint8_t,
        cv.Optional(CONF_BYTES): cv.int_range(1, 4),
        cv.Optional(CONF_MULTIPLIER, default=1.0): cv.float_,
    }).extend(cv.COMPONENT_SCHEMA),
    check_type_or_register,
)


async def to_code(config):
    reg, nbytes = TYPES[config[CONF_TYPE]]
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BUS_T4_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_reg(config.get(CONF_REGISTER, reg)))
    cg.add(var.set_bytes(config.get(CONF_BYTES, nbytes)))
    cg.add(var.set_multiplier(config[CONF_MULTIPLIER]))
    cg.add(parent.register_param(var))
