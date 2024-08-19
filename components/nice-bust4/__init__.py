import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome.components import time

DEPENDENCIES = ["uart"]
AUTO_LOAD = ['uart', 'cover']
CODEOWNERS = ["@srepenko"]
MULTI_CONF = True

nice_bust4_ns = cg.esphome_ns.namespace("nice-bust4")
NICE_BUST4Component = nice_bust4_ns.class_('NICE_BUST4Component', cg.PollingComponent)
MULTI_CONF = True

NICE_BUST4 = nice_bust4_ns.class_(
    "NICE_BUST4", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(NICE_BUST4)})
    .extend(cv.polling_component_schema("1000ms"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

