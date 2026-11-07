import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE
from . import bus_t4_ns, PARENT_SCHEMA, CONF_BUS_T4_ID

BusT4Button = bus_t4_ns.class_("BusT4Button", button.Button, cg.Component)

# значения совпадают с enum BusT4ButtonAction в bus_t4_entities.h
TYPES = {
    "learn_positions": 0,       # обучение / распознавание длины створки
    "learn_bluebus": 1,         # распознавание устройств BlueBus (фотоэлементы и т.п.)
    "cancel_maintenance": 2,    # сброс обслуживания
    "refresh": 3,               # перечитать все настройки из привода
    "step_by_step": 4,
    "stop": 5,
    "open": 6,
    "close": 7,
    "partial_open": 8,
}

CONFIG_SCHEMA = button.button_schema(BusT4Button).extend(PARENT_SCHEMA).extend({
    cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BUS_T4_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_action(TYPES[config[CONF_TYPE]]))
