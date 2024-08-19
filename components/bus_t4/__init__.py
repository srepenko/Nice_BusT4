import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome.components import time

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@srepenko"]
MULTI_CONF = True

niceBusT4_ns = cg.esphome_ns.namespace("niceBusT4")
#InverterComponent = inverter_ns.class_("Inverter", cg.Component)
NiceBusT4 = niceBusT4_ns.class_(
    "NiceBusT4_component", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusT4)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

