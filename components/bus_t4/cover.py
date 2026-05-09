import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_USE_ADDRESS, CONF_RX_PIN, CONF_TX_PIN

CONF_OXI_ADDRESS = "oxi_address"
CONF_UART_NUM    = "uart_num"

bus_t4_ns = cg.esphome_ns.namespace('bus_t4')
Nice = bus_t4_ns.class_('NiceBusT4', cover.Cover, cg.Component)

CONFIG_SCHEMA = cover.COVER_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Nice),
    cv.Optional(CONF_ADDRESS):     cv.hex_uint16_t,
    cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_OXI_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_RX_PIN):      cv.uint8_t,
    cv.Optional(CONF_TX_PIN):      cv.uint8_t,
    # Номер UART: 0=Serial, 1=Serial1 (по умолчанию), 2=Serial2
    cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=0, max=2),
}).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield cover.register_cover(var, config)

    if CONF_ADDRESS in config:
        cg.add(var.set_to_address(config[CONF_ADDRESS]))

    if CONF_USE_ADDRESS in config:
        cg.add(var.set_from_address(config[CONF_USE_ADDRESS]))

    if CONF_OXI_ADDRESS in config:
        cg.add(var.set_oxi_address(config[CONF_OXI_ADDRESS]))

    if CONF_RX_PIN in config:
        cg.add(var.set_rx_pin(config[CONF_RX_PIN]))

    if CONF_TX_PIN in config:
        cg.add(var.set_tx_pin(config[CONF_TX_PIN]))

    cg.add(var.set_uart_num(config[CONF_UART_NUM]))
