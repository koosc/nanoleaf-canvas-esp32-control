import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@ckoos"]
DEPENDENCIES = ["network"]

nanoleaf_canvas_ns = cg.esphome_ns.namespace("nanoleaf_canvas")
NanoleafCanvas = nanoleaf_canvas_ns.class_("NanoleafCanvas", cg.Component)

CONF_NANOLEAF_CANVAS_ID = "nanoleaf_canvas_id"
CONF_DATA_PIN = "data_pin"
CONF_PUSH_PULL_TX = "push_pull_tx"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NanoleafCanvas),
        cv.Optional(CONF_DATA_PIN, default=4): cv.int_,
        # Drive the data line actively while transmitting. With open drain only,
        # rising edges are too slow and panels drop frames.
        cv.Optional(CONF_PUSH_PULL_TX, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_data_pin(config[CONF_DATA_PIN]))
    cg.add(var.set_push_pull_tx(config[CONF_PUSH_PULL_TX]))
