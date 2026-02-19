import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import uart, text_sensor
from esphome.const import CONF_ID, CONF_NAME, CONF_TRIGGER_ID

DEPENDENCIES = ["uart"]

CONF_READ_ON_BOOT = "read_on_boot"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_CONFIG_TEXT_SENSOR = "config_text_sensor"
CONF_TX_MESSAGE = "tx_message"
CONF_SEND_ON_BOOT = "send_on_boot"
CONF_SEND_INTERVAL = "send_interval"
CONF_RX_LOG = "rx_log"
CONF_FIXED_TX = "fixed_tx"
CONF_TARGET_ADDR = "target_addr"
CONF_TARGET_CH = "target_ch"
CONF_ON_RECEIVE = "on_receive"
CONF_ON_MESSAGE = "on_message"
CONF_ON_PING_ACK = "on_ping_ack"
CONF_ON_PING_TIMEOUT = "on_ping_timeout"
CONF_ON_PING = "on_ping"
CONF_ON_MSG_ACK = "on_msg_ack"
CONF_PING_INTERVAL = "ping_interval"
CONF_ACK_TIMEOUT = "ack_timeout"
CONF_PING_TARGETS = "ping_targets"
CONF_ONLINE_TEXT_SENSOR = "online_text_sensor"
CONF_RSSI_TEXT_SENSOR = "rssi_text_sensor"
CONF_ONLINE_TARGET_TEXT_SENSORS = "online_target_text_sensors"

CONF_CONFIG = "config"
CONF_ADDR = "addr"
CONF_CH = "ch"
CONF_CRYPT = "crypt"
CONF_AUTO_WRITE = "auto_write"
CONF_REGISTER_SPED = "register_sped"
CONF_REGISTER_OPTION = "register_option"
CONF_REGISTER_FEATURES = "register_features"

lora_e220_ns = cg.esphome_ns.namespace("lora_e220")
LoRaE220 = lora_e220_ns.class_("LoRaE220", cg.Component, uart.UARTDevice)
LoRaE220OnReceiveTrigger = lora_e220_ns.class_(
    "LoRaE220OnReceiveTrigger", automation.Trigger.template(cg.std_string)
)
LoRaE220OnMessageTrigger = lora_e220_ns.class_(
    "LoRaE220OnMessageTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
LoRaE220OnPingAckTrigger = lora_e220_ns.class_(
    "LoRaE220OnPingAckTrigger", automation.Trigger.template(cg.std_string)
)
LoRaE220OnPingTimeoutTrigger = lora_e220_ns.class_(
    "LoRaE220OnPingTimeoutTrigger", automation.Trigger.template(cg.std_string)
)
LoRaE220OnPingTrigger = lora_e220_ns.class_(
    "LoRaE220OnPingTrigger", automation.Trigger.template(cg.std_string)
)
LoRaE220OnMsgAckTrigger = lora_e220_ns.class_(
    "LoRaE220OnMsgAckTrigger", automation.Trigger.template(cg.std_string)
)

CONFIG_BLOCK_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ADDR, default=0x0000): cv.int_range(min=0x0000, max=0xFFFF),
        cv.Optional(CONF_REGISTER_SPED): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_REGISTER_OPTION): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_CH, default=0x02): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_REGISTER_FEATURES): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_CRYPT, default=0x0000): cv.int_range(min=0x0000, max=0xFFFF),
        cv.Optional(CONF_AUTO_WRITE, default=True): cv.boolean,
    }
)

PING_TARGET_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ADDR): cv.int_range(min=0x0000, max=0xFFFF),
        cv.Optional(CONF_CH, default=0x02): cv.int_range(min=0x00, max=0xFF),
    }
)

ONLINE_TARGET_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ADDR): cv.int_range(min=0x0000, max=0xFFFF),
        cv.Required(CONF_CH): cv.int_range(min=0x00, max=0xFF),
        cv.Required("sensor"): text_sensor.text_sensor_schema(),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(LoRaE220),
            cv.Optional(CONF_READ_ON_BOOT, default=True): cv.boolean,
            cv.Optional(CONF_RESPONSE_TIMEOUT, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CONFIG_TEXT_SENSOR): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CONFIG): CONFIG_BLOCK_SCHEMA,
            cv.Optional(CONF_TX_MESSAGE): cv.string,
            cv.Optional(CONF_SEND_ON_BOOT, default=False): cv.boolean,
            cv.Optional(CONF_SEND_INTERVAL, default="0s"): cv.update_interval,  # 0s = wyłącz
            cv.Optional(CONF_RX_LOG, default=True): cv.boolean,
            cv.Optional(CONF_FIXED_TX, default=False): cv.boolean,
            cv.Optional(CONF_TARGET_ADDR, default=0x0000): cv.int_range(min=0x0000, max=0xFFFF),
            cv.Optional(CONF_TARGET_CH, default=0x02): cv.int_range(min=0x00, max=0xFF),
            cv.Optional(CONF_PING_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ACK_TIMEOUT, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PING_TARGETS, default=[]): cv.ensure_list(PING_TARGET_SCHEMA),
            cv.Optional(CONF_ONLINE_TEXT_SENSOR): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_RSSI_TEXT_SENSOR): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_ONLINE_TARGET_TEXT_SENSORS, default=[]): cv.ensure_list(ONLINE_TARGET_SENSOR_SCHEMA),
            cv.Optional(CONF_ON_RECEIVE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnReceiveTrigger),
                }
            ),
            cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnMessageTrigger),
                }
            ),
            cv.Optional(CONF_ON_PING_ACK): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnPingAckTrigger),
                }
            ),
            cv.Optional(CONF_ON_PING_TIMEOUT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnPingTimeoutTrigger),
                }
            ),
            cv.Optional(CONF_ON_PING): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnPingTrigger),
                }
            ),
            cv.Optional(CONF_ON_MSG_ACK): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LoRaE220OnMsgAckTrigger),
                }
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_read_on_boot(config[CONF_READ_ON_BOOT]))
    cg.add(var.set_response_timeout(config[CONF_RESPONSE_TIMEOUT].total_milliseconds))
    if CONF_TX_MESSAGE in config:
        cg.add(var.set_tx_message(config[CONF_TX_MESSAGE]))
    cg.add(var.set_send_on_boot(config[CONF_SEND_ON_BOOT]))
    cg.add(var.set_send_interval_ms(config[CONF_SEND_INTERVAL].total_milliseconds))
    cg.add(var.set_rx_log(config[CONF_RX_LOG]))
    cg.add(var.set_fixed_tx(config[CONF_FIXED_TX]))
    cg.add(var.set_target_addr(config[CONF_TARGET_ADDR]))
    cg.add(var.set_target_ch(config[CONF_TARGET_CH]))
    cg.add(var.set_ping_interval_ms(config[CONF_PING_INTERVAL].total_milliseconds))
    cg.add(var.set_ack_timeout_ms(config[CONF_ACK_TIMEOUT].total_milliseconds))

    for target in config[CONF_PING_TARGETS]:
        cg.add(var.add_ping_target(target[CONF_ADDR], target[CONF_CH]))

    for conf in config.get(CONF_ON_RECEIVE, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "x")], conf)
    for conf in config.get(CONF_ON_MESSAGE, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "src"), (cg.std_string, "msg")], conf)
    for conf in config.get(CONF_ON_PING_ACK, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "src")], conf)
    for conf in config.get(CONF_ON_PING_TIMEOUT, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "src")], conf)
    for conf in config.get(CONF_ON_PING, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "src")], conf)
    for conf in config.get(CONF_ON_MSG_ACK, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "src")], conf)

    if CONF_CONFIG_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CONFIG_TEXT_SENSOR])
        cg.add(var.set_config_text_sensor(sens))
    if CONF_ONLINE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ONLINE_TEXT_SENSOR])
        cg.add(var.set_online_text_sensor(sens))
    if CONF_RSSI_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RSSI_TEXT_SENSOR])
        cg.add(var.set_rssi_text_sensor(sens))
    online_target_sensors = config[CONF_ONLINE_TARGET_TEXT_SENSORS]
    if online_target_sensors:
        for conf in online_target_sensors:
            sens = await text_sensor.new_text_sensor(conf["sensor"])
            cg.add(var.add_online_target_text_sensor(conf[CONF_ADDR], conf[CONF_CH], sens))
    else:
        # Auto-generate per-target online sensors from ping_targets.
        for target in config[CONF_PING_TARGETS]:
            addr = target[CONF_ADDR]
            ch = target[CONF_CH]
            sens_conf = text_sensor.text_sensor_schema()(
                {
                    CONF_ID: cv.declare_id(text_sensor.TextSensor)(f"lora_online_{ch:02x}_{addr:04x}"),
                    CONF_NAME: f"Online 0x{ch:02X} 0x{addr:04X}",
                }
            )
            sens = await text_sensor.new_text_sensor(sens_conf)
            cg.add(var.add_online_target_text_sensor(addr, ch, sens))

    if CONF_CONFIG in config:
        d = config[CONF_CONFIG]
        sped = d.get(CONF_REGISTER_SPED, 0x60)
        option = d.get(CONF_REGISTER_OPTION, 0x20)
        reg3 = d.get(CONF_REGISTER_FEATURES, 0x03)
        cg.add(var.set_config_addr(d[CONF_ADDR]))
        cg.add(var.set_config_sped(sped))
        cg.add(var.set_config_option(option))
        cg.add(var.set_config_ch(d[CONF_CH]))
        cg.add(var.set_config_reg3(reg3))
        cg.add(var.set_config_crypt(d[CONF_CRYPT]))
        cg.add(var.set_auto_write(d[CONF_AUTO_WRITE]))
        cg.add(var.set_has_config(True))
    else:
        cg.add(var.set_has_config(False))
