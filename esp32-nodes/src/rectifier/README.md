# Rectifier — `rectifier_a` / `rectifier_b`

## Real-World Role

High-voltage DC (HVDC) rectifiers convert 480V AC from the PDU into 48V DC for direct delivery to server power supplies, eliminating the AC-to-DC conversion stage inside each individual server and reducing total conversion losses. Centralised rectifier efficiency typically reaches 96–98% at optimal load (~50%), compared to 89–92% for traditional server PSUs. Google, Meta, and many OCP-aligned hyperscale operators have adopted 48V DC rack distribution for new server designs. N+1 or 2N rectifier redundancy ensures continued output if one module fails — both `rectifier_a` and `rectifier_b` independently feed the shared `server_rack` node.

---

## Nodes in This Topology

| node_id        | Side | Input Voltage | Output Voltage | Parent  | Child         |
|----------------|------|---------------|----------------|---------|---------------|
| `rectifier_a`  | A    | 480 V AC      | 48 V DC        | `pdu_a` | `server_rack` |
| `rectifier_b`  | B    | 480 V AC      | 48 V DC        | `pdu_b` | `server_rack` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field          | Type   | Default  | Description                      |
|----------------|--------|----------|----------------------------------|
| `ts`           | string | HH:MM:SS | Timestamp from NTP               |
| `input_v_ac`   | float  | 480.0    | AC input voltage (V)             |
| `output_v_dc`  | float  | 48.0     | DC output voltage (V)            |
| `load_pct`     | int    | 30       | Output load (%)                  |
| `state`        | string | NORMAL   | Rectifier operating state        |
| `ac_voltage`   | int    | 480      | Rated AC input voltage (V)       |
| `dc_voltage`   | int    | 48       | Rated DC output voltage (V)      |

---

## States

| State    | Meaning                                             |
|----------|-----------------------------------------------------|
| `NORMAL` | AC input present, DC output active within limits    |
| `FAULT`  | AC input overvoltage (> 528V) — output suppressed   |
| `OFF`    | AC input below threshold (< 400V) — no DC output   |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example          | Effect                                            |
|------------------|------------------|---------------------------------------------------|
| `INPUT_AC:<v>`   | `INPUT_AC:0`     | Set AC input; < 400V → OFF, > 528V → FAULT        |
| `LOAD:<pct>`     | `LOAD:60`        | Set output load percentage                        |
| `STATUS:<state>` | `STATUS:FAULT`   | Force state string                                |

---

## Auto-Thresholds

| Condition               | Resulting State               |
|-------------------------|-------------------------------|
| `input_v_ac < 400`      | `OFF`; `output_v_dc = 0`      |
| `input_v_ac > 528`      | `FAULT`                       |
| Input restored from OFF | `NORMAL`; `output_v_dc = 48`  |

---

## Build & Flash

```bash
# Side A
pio run -e rectifier_a --target upload

# Side B
pio run -e rectifier_b --target upload

# Build only (no flash)
pio run -e rectifier_a
pio run -e rectifier_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/rectifier_a/status" -v

# Cut AC input (simulates PDU loss — DC output drops to 0)
mosquitto_pub -h 192.168.4.1 -t "winter-river/rectifier_a/control" -m "INPUT_AC:0"

# Restore AC input
mosquitto_pub -h 192.168.4.1 -t "winter-river/rectifier_a/control" -m "INPUT_AC:480"
