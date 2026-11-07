import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@ckoos"]
DEPENDENCIES = ["network"]

nanoleaf_sniffer_ns = cg.esphome_ns.namespace("nanoleaf_sniffer")
NanoleafSniffer = nanoleaf_sniffer_ns.class_("NanoleafSniffer", cg.Component)

CONF_DATA_PIN = "data_pin"
CONF_FILTER_POLLS = "filter_polls"
CONF_EMULATOR_MODE = "emulator_mode"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NanoleafSniffer),
        cv.Optional(CONF_DATA_PIN, default=4): cv.int_,
        cv.Optional(CONF_FILTER_POLLS, default=True): cv.boolean,
        cv.Optional(CONF_EMULATOR_MODE, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_data_pin(config[CONF_DATA_PIN]))
    cg.add(var.set_filter_polls(config[CONF_FILTER_POLLS]))
    cg.add(var.set_emulator_mode(config[CONF_EMULATOR_MODE]))
