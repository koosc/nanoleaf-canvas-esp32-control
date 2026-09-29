import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID
from . import NanoleafCanvas, CONF_NANOLEAF_CANVAS_ID, nanoleaf_canvas_ns

NanoleafCanvasLight = nanoleaf_canvas_ns.class_(
    "NanoleafCanvasLight", light.LightOutput
)

CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(NanoleafCanvasLight),
        cv.GenerateID(CONF_NANOLEAF_CANVAS_ID): cv.use_id(NanoleafCanvas),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_NANOLEAF_CANVAS_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await light.register_light(var, config)
