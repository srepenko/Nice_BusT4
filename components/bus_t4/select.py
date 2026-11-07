import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE, CONF_OPTIONS
from . import bus_t4_ns, PARENT_SCHEMA, CONF_BUS_T4_ID, CONF_REGISTER, check_type_or_register

BusT4Select = bus_t4_ns.class_("BusT4Select", select.Select, cg.Component)

# type: (регистр, {подпись: значение})
TYPES = {
    "standby_mode": (0x8E, {"Safety": 0, "BlueBus": 1, "All": 2}),
    "custom": (None, {}),
}

CONFIG_SCHEMA = cv.All(
    select.select_schema(BusT4Select).extend(PARENT_SCHEMA).extend({
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_REGISTER): cv.hex_uint8_t,
        # для type: custom — свой список: {"Подпись": значение}
        cv.Optional(CONF_OPTIONS): cv.Schema({cv.string: cv.uint8_t}),
    }).extend(cv.COMPONENT_SCHEMA),
    check_type_or_register,
)


async def to_code(config):
    reg, choices = TYPES[config[CONF_TYPE]]
    reg = config.get(CONF_REGISTER, reg)
    choices = config.get(CONF_OPTIONS, choices)
    if not choices:
        raise cv.Invalid("для select нужен options")
    var = await select.new_select(config, options=list(choices.keys()))
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BUS_T4_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_reg(reg))
    for label, value in choices.items():
        cg.add(var.add_choice(label, value))
    cg.add(parent.register_param(var))
