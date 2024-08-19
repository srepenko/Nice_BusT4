import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome.components import time

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@srepenko"]
MULTI_CONF = True

#empty_uart_component_ns = cg.esphome_ns.namespace("empty_uart_component")
#EmptyUARTComponent = empty_uart_component_ns.class_(
#    "EmptyUARTComponent", cg.Component, uart.UARTDevice
#)

#CONFIG_SCHEMA = (
#    cv.Schema({cv.GenerateID(): cv.declare_id(EmptyUARTComponent)})
#    .extend(cv.COMPONENT_SCHEMA)
#    .extend(uart.UART_DEVICE_SCHEMA)
#)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

