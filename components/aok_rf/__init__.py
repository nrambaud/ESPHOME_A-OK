"""A-OK RF protocol component for ESPHome.

ARCHITECTURE
------------
ESPHome's remote_base dump registry is populated at import time inside
the built-in remote_base/__init__.py. External components cannot inject
new dumpers, so 'dump: [aok]' will never work.

This component instead:
  1. Exposes a required top-level 'aok_rf:' YAML key so ESPHome
     loads and compiles the C++ classes.
  2. Attaches an AOKReceiver (a RemoteReceiverListener) to an existing
     remote_receiver component via 'receiver_id:'.
  3. Fires 'on_aok:' automations with optional remote_id/address/command
     filters on each successfully decoded packet.
  4. Registers the 'remote_transmitter.transmit_aok' action.

Decoded frames are always logged via ESP_LOGI — use 'dump: [raw]' on
the remote_receiver for low-level pulse debugging.

YAML EXAMPLE
------------
  aok_rf:
    receiver_id: rf_rx        # required: id of your remote_receiver
    on_aok:
      - remote_id: 0xABCDEF  # all filters are optional
        address:   0x0001
        command:   DOWN
        then:
          - logger.log: "Blind 1 DOWN"

  button:
    - platform: template
      name: "Blind Down"
      on_press:
        - remote_transmitter.transmit_aok:
            transmitter_id: rf_tx
            remote_id: 0xABCDEF
            address:   0x0001
            command:   DOWN
            repeat:              # optional
              times: 5
              wait_time: 10ms
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import remote_base
from esphome.const import CONF_ID, CONF_REPEAT, CONF_TIMES, CONF_WAIT_TIME

CODEOWNERS   = ["@nrambaud"]
AUTO_LOAD    = ["remote_base"]
DEPENDENCIES = ["remote_base"]
MULTI_CONF   = True

# ─── C++ namespace & class references ────────────────────────────────────────
aok_rf_ns = cg.esphome_ns.namespace("aok_rf")

AOKData     = aok_rf_ns.struct("AOKData")
AOKProtocol = aok_rf_ns.class_("AOKProtocol")

# AOKReceiver: Component + RemoteReceiverListener
AOKReceiver = aok_rf_ns.class_(
    "AOKReceiver",
    cg.Component,
    remote_base.RemoteReceiverListener,
)

# AOKTrigger: fires automations with AOKData payload
AOKTrigger = aok_rf_ns.class_(
    "AOKTrigger",
    automation.Trigger.template(AOKData),
)

# AOKAction: used by remote_transmitter.transmit_aok
AOKAction = aok_rf_ns.class_("AOKAction", automation.Action)

# ─── Commands enum (must mirror C++ AOKCommand in aok_protocol.h) ─────────────
AOKCommand = aok_rf_ns.enum("AOKCommand")
AOK_COMMANDS = {
    "UP":      AOKCommand.AOK_CMD_UP,
    "STOP":    AOKCommand.AOK_CMD_STOP,
    "DOWN":    AOKCommand.AOK_CMD_DOWN,
    "PROGRAM": AOKCommand.AOK_CMD_PROGRAM,
}

# ─── Config key constants ─────────────────────────────────────────────────────
CONF_RECEIVER_ID = "receiver_id"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_REMOTE_ID = "remote_id"
CONF_ADDRESS   = "address"
CONF_COMMAND   = "command"

DEFAULT_REPEAT_WAIT_TIME = "25ms"

# ─── on_aok: trigger schema (all filters optional) ────────────────────────────
AOK_TRIGGER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(AOKTrigger),
        cv.Optional(CONF_REMOTE_ID): cv.hex_int_range(min=0, max=0xFFFFFF),
        cv.Optional(CONF_ADDRESS):   cv.hex_int_range(min=0, max=0xFFFF),
        cv.Optional(CONF_COMMAND):   cv.enum(AOK_COMMANDS, upper=True),
    }
)

# ─── Top-level aok_rf: schema ─────────────────────────────────────────────────
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID():                    cv.declare_id(AOKReceiver),
        cv.Required(CONF_RECEIVER_ID):      cv.use_id(remote_base.RemoteReceiverBase),
        cv.Optional("on_aok"):              automation.validate_automation(
                                                AOK_TRIGGER_SCHEMA
                                            ),
    }
).extend(cv.COMPONENT_SCHEMA)


# ─── Code generation ──────────────────────────────────────────────────────────
async def to_code(config):
    # Instantiate AOKReceiver and register it as a component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Attach to the remote_receiver so it receives pulse-train callbacks
    receiver = await cg.get_variable(config[CONF_RECEIVER_ID])
    cg.add(receiver.register_listener(var))

    # Build on_aok: automation triggers
    for trig_conf in config.get("on_aok", []):
        trig = cg.new_Pvariable(trig_conf[CONF_ID])
        await automation.build_automation(trig, [(AOKData, "x")], trig_conf)
        cg.add(var.add_trigger(trig))
        if CONF_REMOTE_ID in trig_conf:
            cg.add(trig.set_remote_id(trig_conf[CONF_REMOTE_ID]))
        if CONF_ADDRESS in trig_conf:
            cg.add(trig.set_address(trig_conf[CONF_ADDRESS]))
        if CONF_COMMAND in trig_conf:
            cg.add(trig.set_command(trig_conf[CONF_COMMAND]))


# ─── remote_transmitter.transmit_aok action ───────────────────────────────────
AOK_TRANSMIT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TRANSMITTER_ID): cv.use_id(
            remote_base.RemoteTransmitterBase
        ),
        cv.Required(CONF_REMOTE_ID): cv.templatable(
            cv.hex_int_range(min=0, max=0xFFFFFF)
        ),
        cv.Required(CONF_ADDRESS): cv.templatable(
            cv.hex_int_range(min=0, max=0xFFFF)
        ),
        cv.Required(CONF_COMMAND): cv.templatable(
            cv.enum(AOK_COMMANDS, upper=True)
        ),
        cv.Optional(CONF_REPEAT): cv.Schema(
            {
                cv.Required(CONF_TIMES): cv.templatable(cv.positive_int),
                cv.Optional(
                    CONF_WAIT_TIME, default=DEFAULT_REPEAT_WAIT_TIME
                ): cv.templatable(cv.positive_time_period_microseconds),
            }
        ),
    }
)


@automation.register_action(
    "remote_transmitter.transmit_aok",
    AOKAction,
    AOK_TRANSMIT_SCHEMA,
)
async def aok_transmit_action_to_code(config, action_id, template_arg, args):
    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    var = cg.new_Pvariable(action_id, template_arg, transmitter)

    templ = await cg.templatable(config[CONF_REMOTE_ID], args, cg.uint32)
    cg.add(var.set_remote_id(templ))
    templ = await cg.templatable(config[CONF_ADDRESS], args, cg.uint16)
    cg.add(var.set_address(templ))
    templ = await cg.templatable(config[CONF_COMMAND], args, cg.uint8)
    cg.add(var.set_command(templ))

    if CONF_REPEAT in config:
        repeat = config[CONF_REPEAT]
        templ = await cg.templatable(repeat[CONF_TIMES], args, cg.uint32)
        cg.add(var.set_send_times(templ))
        templ = await cg.templatable(repeat[CONF_WAIT_TIME], args, cg.uint32)
        cg.add(var.set_send_wait(templ))

    return var
