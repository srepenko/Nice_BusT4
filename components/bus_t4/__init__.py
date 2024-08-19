import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome.components import time

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@srepenko"]
MULTI_CONF = True

niceBusе4_component_ns = cg.esphome_ns.namespace("niceиusе4_component")
NiceBusе4_component = niceBusе4_component_ns.class_(
    "NiceBusе4_component", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusе4_component)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

