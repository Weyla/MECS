import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, canbus, switch
from esphome.const import CONF_ID

DEPENDENCIES = ["canbus"]
AUTO_LOAD = ["binary_sensor", "switch"]

mecs_ns = cg.esphome_ns.namespace("mecs_expansion")
MECSExpansion = mecs_ns.class_("MECSExpansion", cg.Component)
MECSOutputSwitch = mecs_ns.class_("MECSOutputSwitch", switch.Switch)

CONF_CANBUS_ID = "canbus_id"
CONF_NODE = "node"
CONF_CHANNEL = "channel"
CONF_INPUTS = "inputs"
CONF_OUTPUTS = "outputs"

INPUT_SCHEMA = binary_sensor.binary_sensor_schema().extend({
    cv.Required(CONF_NODE): cv.int_range(min=1, max=127),
    cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=3),
})
OUTPUT_SCHEMA = switch.switch_schema(MECSOutputSwitch).extend({
    cv.Required(CONF_NODE): cv.int_range(min=1, max=127),
    cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=3),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MECSExpansion),
    cv.Required(CONF_CANBUS_ID): cv.use_id(canbus.Canbus),
    cv.Optional(CONF_INPUTS, default=[]): cv.ensure_list(INPUT_SCHEMA),
    cv.Optional(CONF_OUTPUTS, default=[]): cv.ensure_list(OUTPUT_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config[CONF_CANBUS_ID])
    cg.add(var.set_canbus(bus))

    for item in config[CONF_INPUTS]:
        sensor = await binary_sensor.new_binary_sensor(item)
        cg.add(var.add_input(item[CONF_NODE], item[CONF_CHANNEL], sensor))

    for item in config[CONF_OUTPUTS]:
        output = await switch.new_switch(item)
        cg.add(output.set_parent(var))
        cg.add(output.set_node(item[CONF_NODE]))
        cg.add(output.set_channel(item[CONF_CHANNEL]))
        cg.add(var.add_output(output))
