# ESPHome M5Stack LoRa E220 (UART) External Component

ESPHome external component for M5Stack E220 LoRa UART modules.

Features:
- Read and write E220 module configuration over UART (`C1`/`C0` commands).
- Fixed point-to-point messaging (`fixed_tx`, `target_addr`, `target_ch`).
- Protocol-level frames: `PING`, `ACK`, `MSG`, `MSG_ACK`.
- Link monitoring (heartbeat) with configurable `ping_interval` and `ack_timeout`.
- Online state as text sensors (`"true"` / `"false"`), including per-target sensors.
- RSSI reporting to text sensor (e.g. `-78 dBm`) when RSSI byte is enabled in `reg3`.
- ESPHome automations: `on_receive`, `on_message`, `on_ping`, `on_ping_ack`, `on_ping_timeout`, `on_msg_ack`.
- Runtime API from lambdas: `id(e220).read_config()`, `id(e220).send("...")`, `id(e220).send_msg()`.

## Installation

## 1) Add external component in ESPHome YAML

Example for a GitHub repository:

```yaml
external_components:
  - source: github://grandalos/m5stack-lora-e220
    components: [lora_e220]
```

## 2) Configure UART

```yaml
uart:
  id: uart_lora
  baud_rate: 9600
  tx_pin: GPIO02
  rx_pin: GPIO01
  stop_bits: 1
  parity: NONE
```

## Component Configuration

```yaml
lora_e220:
  id: e220
  uart_id: uart_lora
```

### Main options

- `id` (required): component ID.
- `uart_id` (required): UART bus ID.
- `read_on_boot` (default: `true`): read E220 config after boot.
- `response_timeout` (default: `500ms`): timeout for config command response.
- `config_text_sensor`: publishes full config string (`ADDR/SPED/OPT/CH/REG3/CRYPT`).
- `rx_log` (default: `true`): debug logging of received lines.

### Desired module config

```yaml
desired_config:
  addr: 0x0001
  sped: 0x60
  option: 0x20
  ch: 0x02
  reg3: 0xC3
  crypt: 0x0000
  auto_write: true
```

- `addr` (0x0000..0xFFFF)
- `sped` (0x00..0xFF)
- `option` (0x00..0xFF)
- `ch` (0x00..0xFF)
- `reg3` (0x00..0xFF)
- `crypt` (0x0000..0xFFFF)
- `auto_write` (default: `true`)

Notes:
- `crypt` on E220 is write-only in practice, and read-back may return `0x0000`.
- Verification logic handles this behavior (CRYPT mismatch is ignored when all other fields match and desired CRYPT is non-zero).

### TX options

- `tx_message`: periodic/on-boot payload.
- `send_on_boot` (default: `false`)
- `fixed_tx` (default: `false`): required for address/channel-targeted frames.
- `target_addr` (default: `0x0000`)
- `target_ch` (default: `0x02`)

### Heartbeat / online options

- `ping_interval` (default: `5s`)
- `ack_timeout` (default: `500ms`)
- `ping_targets`: list of nodes to ping (receiver role).
- `online_text_sensor`: aggregated online state (`true` only when all targets are online in receiver role).
- `online_target_text_sensors`: optional explicit per-target text sensors.

If `online_target_text_sensors` is omitted, sensors are auto-generated from `ping_targets` with names:
- `Online 0xCH 0xADDR`

### RSSI

- `rssi_text_sensor`: publishes packet RSSI in dBm text.
- Requires RSSI byte in module settings (`reg3` bit 7 set, e.g. `0xC3`).

Behavior:
- Receiver role (node with `ping_targets`): RSSI from incoming `ACK`.
- Sender role (node without `ping_targets`): RSSI from incoming `PING`.

## Triggers

### `on_receive`
Triggered on every received text line.

Args:
- `x` (`std::string`)

### `on_message`
Triggered for protocol message `MSG|<src>|<payload>`.

Args:
- `src` (`std::string`, format `0x0001`)
- `msg` (`std::string`)

### `on_ping`
Triggered when `PING` is received.

Args:
- `src` (`std::string`)

### `on_ping_ack`
Triggered when a matching `ACK` to previously sent `PING` is received.

Args:
- `src` (`std::string`)

### `on_ping_timeout`
Triggered when `ACK` is not received before `ack_timeout`.

Args:
- `src` (`std::string`)

### `on_msg_ack`
Triggered when `MSG_ACK` for a sent `MSG` is received.

Args:
- `src` (`std::string`)

## Example: Receiver (pings senders)

```yaml
lora_e220:
  id: e220
  uart_id: uart_lora
  read_on_boot: true
  fixed_tx: true
  target_addr: 0x0001
  target_ch: 0x02
  ping_interval: 5s
  ack_timeout: 1500ms
  rx_log: true

  desired_config:
    addr: 0x0002
    sped: 0x60
    option: 0x20
    ch: 0x02
    reg3: 0xC3
    crypt: 0x0000
    auto_write: true

  config_text_sensor:
    name: "E220 Config"
  online_text_sensor:
    name: "Online"
  rssi_text_sensor:
    name: "LoRa RSSI"

  ping_targets:
    - addr: 0x0001
      ch: 0x02

  on_ping_ack:
    then:
      - logger.log:
          format: "ACK OK from %s"
          args: ['src.c_str()']
          level: DEBUG

  on_ping_timeout:
    then:
      - logger.log:
          format: "ACK TIMEOUT from %s"
          args: ['src.c_str()']
          level: DEBUG

  on_message:
    then:
      - logger.log:
          format: "RX from %s: %s"
          args: ['src.c_str()', 'msg.c_str()']
          level: INFO
```

## Example: Sender (responds to PING, can send MSG)

```yaml
lora_e220:
  id: e220
  uart_id: uart_lora
  read_on_boot: true
  fixed_tx: true
  target_addr: 0x0002
  target_ch: 0x02
  ping_interval: 5s
  ack_timeout: 500ms
  rx_log: true

  desired_config:
    addr: 0x0001
    sped: 0x60
    option: 0x20
    ch: 0x02
    reg3: 0xC3
    crypt: 0x0000
    auto_write: true

  config_text_sensor:
    name: "E220 Config"
  online_text_sensor:
    name: "Online"
  rssi_text_sensor:
    name: "LoRa RSSI"

  on_ping:
    then:
      - logger.log:
          format: "PING from %s"
          args: ['src.c_str()']
          level: DEBUG

  on_msg_ack:
    then:
      - logger.log:
          format: "MSG_ACK from %s"
          args: ['src.c_str()']
          level: INFO
```

## Sending from automations

Use lambda calls:

```yaml
button:
  - platform: template
    name: "Read E220 config"
    on_press:
      - lambda: id(e220).read_config();

  - platform: template
    name: "Send custom MSG"
    on_press:
      - lambda: id(e220).send("HELLO WORLD");
```

`send("...")` sends a protocol frame `MSG|<self_addr>|<payload>` and waits for `MSG_ACK`.

## Protocol summary

- Heartbeat:
  - `PING|<seq_dec>|<src_addr_hex4>|<src_ch_hex2>`
  - `ACK|<src_addr_hex4>|<seq_dec>`
- User message:
  - `MSG|<src_addr_hex4>|<payload>`
  - `MSG_ACK|<src_addr_hex4>`

## Role model

- Receiver role: node with non-empty `ping_targets`.
  - Sends periodic `PING`.
  - Marks each target online/offline based on matching `ACK`.
- Sender role: node without `ping_targets`.
  - Answers every `PING` with `ACK`.
  - Sets own `online_text_sensor` to `true` when `PING` is seen, and back to `false` after timeout.
  - Answers every incoming `MSG` with `MSG_ACK`.

## Hardware note (M0/M1)

If your board uses physical M0/M1 switches for mode selection, keep read/write config flow as-is and ensure module mode matches the command being used.

