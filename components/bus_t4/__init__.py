import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover

# Сущности настроек привода (number/switch/select/sensor/button) используют эти платформы,
# поэтому подгружаем их ядро автоматически.
AUTO_LOAD = ["number", "switch", "select", "sensor", "button"]

CONF_BUS_T4_ID = "bus_t4_id"
CONF_REGISTER = "register"
CONF_BYTES = "bytes"
CONF_MULTIPLIER = "multiplier"

bus_t4_ns = cg.esphome_ns.namespace("bus_t4")
NiceBusT4 = bus_t4_ns.class_("NiceBusT4", cover.Cover, cg.Component)

PARENT_SCHEMA = cv.Schema({cv.GenerateID(CONF_BUS_T4_ID): cv.use_id(NiceBusT4)})


def check_type_or_register(config):
    """type: <готовый параметр> или type: custom + register: 0x.."""
    if config["type"] == "custom" and CONF_REGISTER not in config:
        raise cv.Invalid("для type: custom нужен register: 0x..")
    return config
