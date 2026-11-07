import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_TYPE, CONF_UNIT_OF_MEASUREMENT, CONF_MIN_VALUE, CONF_MAX_VALUE, CONF_STEP
from . import (bus_t4_ns, PARENT_SCHEMA, CONF_BUS_T4_ID, CONF_REGISTER, CONF_BYTES, CONF_MULTIPLIER,
               check_type_or_register)

BusT4Number = bus_t4_ns.class_("BusT4Number", number.Number, cg.Component)

# type: (регистр, байт, множитель, min, max, шаг, единица)
TYPES = {
    "pause_time":          (0x81, 1, 1, 0, 250, 1, "s"),   # время паузы перед автозакрытием
    "photo_close_time":    (0x85, 1, 1, 0, 250, 1, "s"),   # закрыть после фото — ожидание
    "always_close_time":   (0x89, 1, 1, 0, 250, 1, "s"),   # всегда закрывать — ожидание
    "standby_time":        (0x8D, 1, 1, 0, 250, 1, "s"),   # режим ожидания — через сколько
    "preflash_open_time":  (0x95, 1, 1, 0, 10, 1, "s"),    # предмерцание при открывании
    "preflash_close_time": (0x99, 1, 1, 0, 10, 1, "s"),    # предмерцание при закрывании
    "lamp_time":           (0x5B, 1, 1, 0, 250, 1, "s"),   # подсветка
    "lock_time":           (0x5A, 1, 1, 0, 30, 1, "s"),    # электрозамок
    "suction_cup_time":    (0x5C, 1, 1, 0, 30, 1, "s"),    # электромагнитная присоска
    "speed_open":          (0x42, 1, 1, 1, 8, 1, ""),      # скорость открывания (уровень)
    "speed_close":         (0x43, 1, 1, 1, 8, 1, ""),
    "speed_slow_open":     (0x45, 1, 1, 1, 8, 1, ""),      # скорость замедления
    "speed_slow_close":    (0x46, 1, 1, 1, 8, 1, ""),
    "force_open":          (0x4A, 1, 1, 1, 8, 1, ""),      # усилие открывания (уровень)
    "force_close":         (0x4B, 1, 1, 1, 8, 1, ""),
    "leaf_delay_open":     (0x28, 1, 1, 0, 100, 1, ""),    # задержка створки при открывании
    "leaf_delay_close":    (0x29, 1, 1, 0, 100, 1, ""),    # задержка створки при закрывании
    "reverse_time":        (0x31, 1, 1, 0, 250, 1, ""),    # длительность реверса
    "custom":              (None, 1, 1, 0, 255, 1, ""),
}

CONFIG_SCHEMA = cv.All(
    number.number_schema(BusT4Number).extend(PARENT_SCHEMA).extend({
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_REGISTER): cv.hex_uint8_t,
        cv.Optional(CONF_BYTES): cv.int_range(1, 4),
        cv.Optional(CONF_MULTIPLIER): cv.float_,
        cv.Optional(CONF_MIN_VALUE): cv.float_,
        cv.Optional(CONF_MAX_VALUE): cv.float_,
        cv.Optional(CONF_STEP): cv.positive_float,
    }).extend(cv.COMPONENT_SCHEMA),
    check_type_or_register,
)


async def to_code(config):
    reg, nbytes, mult, mn, mx, step, unit = TYPES[config[CONF_TYPE]]
    reg = config.get(CONF_REGISTER, reg)
    if unit and CONF_UNIT_OF_MEASUREMENT not in config:
        config = {**config, CONF_UNIT_OF_MEASUREMENT: unit}   # единицу задаёт ESPHome штатно
    var = await number.new_number(config, min_value=config.get(CONF_MIN_VALUE, mn),
                                  max_value=config.get(CONF_MAX_VALUE, mx), step=config.get(CONF_STEP, step))
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BUS_T4_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_reg(reg))
    cg.add(var.set_bytes(config.get(CONF_BYTES, nbytes)))
    cg.add(var.set_multiplier(config.get(CONF_MULTIPLIER, mult)))
    cg.add(parent.register_param(var))
