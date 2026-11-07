import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_USE_ADDRESS, CONF_RX_PIN, CONF_TX_PIN

from . import NiceBusT4 as Nice

CONF_PARAMS_INTERVAL = "params_interval"

_base = cover.cover_schema(Nice) if hasattr(cover, "cover_schema") else cover.COVER_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(Nice)})

CONFIG_SCHEMA = _base.extend({
    cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_RX_PIN): cv.uint8_t,
    cv.Optional(CONF_TX_PIN): cv.uint8_t,
    # как часто перечитывать настройки привода (параметры number/switch/select/sensor)
    cv.Optional(CONF_PARAMS_INTERVAL, default="5min"): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)
    cg.add(var.set_params_interval(config[CONF_PARAMS_INTERVAL]))

    if CONF_ADDRESS in config:
        cg.add(var.set_to_address(config[CONF_ADDRESS]))
    if CONF_USE_ADDRESS in config:
        cg.add(var.set_from_address(config[CONF_USE_ADDRESS]))
    if CONF_RX_PIN in config:
        cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    if CONF_TX_PIN in config:
        cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
