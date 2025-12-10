import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# esp32_ble needed for BT compilation, but we disable enable_on_boot in yaml
DEPENDENCIES = ["esp32_ble", "text"]
AUTO_LOAD = ["text_sensor", "switch", "button"]
CODEOWNERS = ["@esphome"]

CONF_BLE_NAME = "ble_name"
CONF_START_ON_BOOT = "start_on_boot"

irk_capture_ns = cg.esphome_ns.namespace("irk_capture")
IRKCaptureComponent = irk_capture_ns.class_("IRKCaptureComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(IRKCaptureComponent),
    cv.Optional(CONF_BLE_NAME, default="Beats Solo4"): cv.string,
    cv.Optional(CONF_START_ON_BOOT, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_ble_name(config[CONF_BLE_NAME]))
    cg.add(var.set_start_on_boot(config[CONF_START_ON_BOOT]))
    
    cg.add_define("USE_TEXT")
    
    # NimBLE-Arduino library
    cg.add_library("h2zero/NimBLE-Arduino", "2.2.1")
    
    # NimBLE build flags
    cg.add_build_flag("-DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=1")
    cg.add_build_flag("-DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1")
    cg.add_build_flag("-DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0")
    cg.add_build_flag("-DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0")
    cg.add_build_flag("-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=3")
