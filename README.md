# ESPHOME_A-OK

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.2+-blue?logo=esphome)](https://esphome.io)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Custom ESPHome external component implementing the **A-OK 433 MHz OOK protocol**
used by A-OK motorised blinds and roller-shutter motors.

The component hooks into ESPHome's `remote_transmitter` / `remote_receiver`
infrastructure via a dedicated `aok_rf:` top-level key, and exposes:

- **`remote_transmitter.transmit_aok`** — send any A-OK command
- **`on_aok:`** — automation trigger fired on received A-OK packets, with optional filtering

> **Note on `dump: [aok]`:** ESPHome's built-in dump registry cannot be
> extended by external components. Use `dump: [raw]` instead — decoded A-OK
> frames are logged automatically by the `aok_rf` component.

---

## Protocol

```
← Sync ──────→ ←──────────────── 64 data bits ─────────────────→ ←T1→

 ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
|    ~5100 µs HIGH     |__ ~600 µs LOW __|  bit0  bit1 … bit63  | 1 |

Bit '0':  ‾|__  →  HIGH  300 µs  +  LOW  600 µs   (1× + 2× base)
Bit '1':  ‾‾|_  →  HIGH  600 µs  +  LOW  300 µs   (2× + 1× base)
```

| Field      | Bits | Value                                           |
|------------|------|-------------------------------------------------|
| Start      |  8   | Always `0xA3`                                   |
| Remote ID  | 24   | Unique per remote                               |
| Address    | 16   | Channel bitmask — bit 0 = ch1, …, bit 15 = ch16 |
| Command    |  8   | See table below                                 |
| Checksum   |  8   | `Σ(ID bytes + ADDR bytes + CMD) & 0xFF`         |
| Trailing 1 |  1   | Always transmitted last                         |

**Total: 65 bits transmitted.**

### Commands

| YAML name   | Hex    | Description                              |
|-------------|--------|------------------------------------------|
| `UP`        | `0x0B` | Open / raise                             |
| `STOP`      | `0x23` | Stop                                     |
| `DOWN`      | `0x43` | Close / lower                            |
| `PROGRAM`   | `0x53` | Enter programming mode                   |

> **Note:** These values were confirmed by sniffing a real A-OK remote.
> Other command codes may exist depending on the motor model — use
> `dump: [raw]` and press each button on your remote to discover them.

---

## Hardware

This component targets an **ESP32-WROOM-32D** paired with a **CC1101** 433 MHz
transceiver. The CC1101 communicates over SPI and is configured by the
built-in ESPHome `cc1101` component, which handles frequency, modulation and
power settings automatically.

> ⚠️ **The CC1101 is a 3.3 V device.** The ESP32-WROOM-32D also runs at 3.3 V,
> so no level-shifter is needed. **Never connect VCC to 5 V.**

### Wiring

| CC1101 Pin | Function                     | ESP32-WROOM-32D Pin    |
|------------|------------------------------|------------------------|
| `VCC`      | Power supply                 | **3V3**                |
| `GND`      | Ground                       | **GND**                |
| `SCK`      | SPI clock                    | **GPIO18** (VSPI CLK)  |
| `MOSI`     | SPI master out               | **GPIO23** (VSPI MOSI) |
| `MISO`     | SPI master in                | **GPIO19** (VSPI MISO) |
| `CSN`      | SPI chip select (active low) | **GPIO05**             |
| `GDO0`     | TX data out                  | **GPIO12**             |
| `GDO2`     | RX data out                  | **GPIO27**             |

```
ESP32-WROOM-32D          CC1101
───────────────          ──────
3V3    ─────────────────  VCC
GND    ─────────────────  GND
GPIO18 ─────────────────  SCK    (VSPI CLK)
GPIO23 ─────────────────  MOSI   (VSPI MOSI)
GPIO19 ─────────────────  MISO   (VSPI MISO)
GPIO05 ─────────────────  CSN    (Chip Select, active low)
GPIO12 ─────────────────  GDO0   ← TX data  (driven by remote_transmitter)
GPIO27 ─────────────────  GDO2   ← RX data  (read by remote_receiver)
                          ANT    ← solder a 17.3 cm wire for 433 MHz
```

**Why GDO0 and GDO2 on separate pins?** The CC1101 uses GDO0 for transmission
and GDO2 for reception, so TX and RX operate simultaneously without contention.

> ⚠️ **Strapping pins — GPIO05 (CSN) and GPIO12 (GDO0)** will produce a
> warning in the ESPHome log. This is **safe** in this configuration: the
> CC1101 adds no external pull-up or pull-down resistors on those lines, so
> the ESP32 boots normally. The warnings can be ignored.

---

## Installation

Add both external components to your ESPHome YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/nrambaud/ESPHOME_A-OK
      ref: main
    components: [aok_rf]
    refresh: 1d
```

The `cc1101` component is bundled with ESPHome — no extra source needed.

---

## Quick start

### Step 1 — Minimal YAML skeleton

```yaml
spi:
  clk_pin:  GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

cc1101:
  cs_pin: GPIO05
  frequency: 433.92MHz
  output_power: 1
  modulation_type: ASK/OOK

remote_transmitter:
  id: rf_tx
  pin: GPIO12
  carrier_duty_percent: 100%
  on_transmit:
    then:
      - cc1101.begin_tx
  on_complete:
    then:
      - cc1101.begin_rx

remote_receiver:
  id: rf_rx
  pin: GPIO27
  dump: [raw]          # ← use raw, NOT aok (see note above)
  tolerance: 40%
  filter: 100us
  idle: 10ms

aok_rf:                # ← required top-level key to load the component
  receiver_id: rf_rx
```

### Step 2 — Sniff your existing remote

With `dump: [raw]` the console will print raw pulse timings. The `aok_rf`
component also automatically logs every successfully decoded A-OK frame:

```
[I][aok_rf]: Received A-OK: remote_id=0xABCDEF address=0x0001 command=DOWN (0x43)
```

Point your original remote at the CC1101 antenna and note the `remote_id`.
Use that value in your transmitter config to **clone the remote**.

### Step 3 — Transmit a command

```yaml
# In any automation or action:
- remote_transmitter.transmit_aok:
    transmitter_id: rf_tx
    remote_id: 0xABCDEF   # from the sniffed log
    address: 0x0001        # channel 1
    command: DOWN
```

### Step 4 — React to received packets with `on_aok`

```yaml
aok_rf:
  receiver_id: rf_rx
  on_aok:
    # No filter → fires on every valid A-OK packet
    - then:
        - lambda: |-
            ESP_LOGI("aok", "id=0x%06X addr=0x%04X cmd=0x%02X",
                     x.remote_id, x.address, x.command);

    # Filter on remote + channel + command
    - remote_id: 0xABCDEF
      address: 0x0001
      command: DOWN
      then:
        - logger.log: "Physical remote → Blind 1 DOWN"
```

All three filter fields (`remote_id`, `address`, `command`) are optional and
can be combined freely.

### Cover entity (Home Assistant)

```yaml
cover:
  - platform: template
    name: "Living Room Blind"
    device_class: blind
    optimistic: true
    open_action:
      - remote_transmitter.transmit_aok:
          transmitter_id: rf_tx
          remote_id: 0xABCDEF
          address: 0x0001
          command: UP
    stop_action:
      - remote_transmitter.transmit_aok:
          transmitter_id: rf_tx
          remote_id: 0xABCDEF
          address: 0x0001
          command: STOP
    close_action:
      - remote_transmitter.transmit_aok:
          transmitter_id: rf_tx
          remote_id: 0xABCDEF
          address: 0x0001
          command: DOWN
```

### Multi-channel (address bitmask)

The `address` field is a bitmask — set multiple bits to trigger several
channels simultaneously:

```yaml
- remote_transmitter.transmit_aok:
    transmitter_id: rf_tx
    remote_id: 0xABCDEF
    address: 0x000F    # channels 1, 2, 3 and 4 at once
    command: DOWN
```

---

## Pairing a new remote ID to a motor

1. Hold the motor's **PROGRAM** button until it beeps / jiggles (≈5 s).
2. Send the `PROGRAM` command from ESPHome with your chosen `remote_id`.
3. The motor confirms with a double-jiggle.
4. Test with `UP` / `DOWN`.

---

## File structure

```
components/
└── aok_rf/
    ├── __init__.py        ← ESPHome Python schema, component registration & code-gen
    ├── aok_protocol.h     ← AOKData struct + AOKProtocol encode/decode/dump
    ├── aok_protocol.cpp   ← Protocol implementation
    └── aok_automation.h   ← AOKReceiver (listener), AOKTrigger, AOKAction
aok_example.yaml           ← Full annotated configuration example
```

---

## Troubleshooting

| Symptom | Solution |
|---------|----------|
| `Unable to find dumper 'aok'` | This is a known ESPHome limitation for external components. Replace `dump: [aok]` with `dump: [raw]` and add the `aok_rf:` top-level key — decoded frames are logged automatically |
| `aok_rf: component not found` | Make sure `external_components:` appears in YAML and do **Clean Build Files** in ESPHome Device Builder |
| `GPIO5 / GPIO12 strapping pin` warning | **Safe to ignore** — the CC1101 adds no pull resistors on those pins, the ESP32 boots normally |
| `Component not found: cc1101` | `cc1101` is built into ESPHome — remove it from `external_components` if present, then **Clean Build Files** |
| `Failed to download external component` | Check that Home Assistant has internet access; try setting `refresh: 0s` to force a re-download |
| Motor doesn't respond | Verify `remote_id` matches a paired remote; check all SPI wiring connections |
| Receiver never decodes | Increase `tolerance` to `45%`; verify CC1101 VCC is 3.3 V |
| Checksum mismatch in logs | Electrical noise — add a 100 nF decoupling capacitor between CC1101 VCC and GND |
| Only works at very close range | Check the 17.3 cm antenna wire on the CC1101 ANT pad; increase `output_power` |

---

## License

MIT © nrambaud
