import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE
from . import bus_t4_ns, PARENT_SCHEMA, CONF_BUS_T4_ID, CONF_REGISTER, check_type_or_register

BusT4Switch = bus_t4_ns.class_("BusT4Switch", switch.Switch, cg.Component)

TYPES = {
    "auto_close":    0x80,  # автозакрытие (L1)
    "photo_close":   0x84,  # закрыть после фото (L2)
    "always_close":  0x88,  # всегда закрывать (L3)
    "standby":       0x8C,  # режим ожидания / энергосбережение (L4)
    "preflash":      0x94,  # предмерцание (L6)
    "operator_block": 0x9A, # блокировка привода
    "key_lock":      0x9C,  # блокировка кнопок
    "custom":        None,
}

CONFIG_SCHEMA = cv.All(
    switch.switch_schema(BusT4Switch).extend(PARENT_SCHEMA).extend({
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_REGISTER): cv.hex_uint8_t,
    }).extend(cv.COMPONENT_SCHEMA),
    check_type_or_register,
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BUS_T4_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_reg(config.get(CONF_REGISTER, TYPES[config[CONF_TYPE]])))
    cg.add(parent.register_param(var))
