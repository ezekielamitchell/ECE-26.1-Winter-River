# Lighting Circuit — `lighting_a` / `lighting_b`

## Real-World Role

Data center lighting is fed from 277V single-phase circuits derived from the 480Y/277V distribution system — the phase-to-neutral voltage of a 480V wye system is 277V, which is standard in North American commercial and industrial buildings and more efficient for fluorescent and LED fixture ballasts than 120V. Emergency lighting circuits are typically on a separate branch backed by a dedicated small UPS or battery pack. While lighting is a minor load in data centers (typically under 1% of total facility power), it must remain live during maintenance windows and emergency response procedures where visibility is safety-critical.

---

## Nodes in This Topology

| node_id      | Side | Rated Voltage | Parent      | Children |
|--------------|------|---------------|-------------|----------|
| `lighting_a` | A    | 277 V AC      | `lv_dist_a` | none     |
| `lighting_b` | B    | 277 V AC      | `lv_dist_b` | none     |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field        | Type   | Default  | Description                            |
|--------------|--------|----------|----------------------------------------|
| `ts`         | string | HH:MM:SS | Timestamp from NTP                     |
| `input_v`    | float  | 277.0    | Supply voltage (V)                     |
| `load_pct`   | int    | 45       | Electrical load as % of rated capacity |
| `dimmer_pct` | int    | 80       | Dimmer / occupancy control setting (%) |
| `state`      | string | ON       | Lighting circuit state                 |
| `voltage`    | int    | 277      | Rated voltage (V)                      |

---

## States

| State   | Meaning                                          |
|---------|--------------------------------------------------|
| `ON`    | Circuit powered and lighting active              |
| `OFF`   | No input power or circuit switched off           |
| `FAULT` | Input voltage anomaly detected                   |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example      | Effect                                                        |
|------------------|--------------|---------------------------------------------------------------|
| `INPUT:<v>`      | `INPUT:0`    | Set input voltage; < 240V transitions to OFF                  |
| `DIM:<pct>`      | `DIM:50`     | Set dimmer level %; `load_pct` scales proportionally          |
| `STATUS:ON`      | `STATUS:ON`  | Turn lights on                                                |
| `STATUS:OFF`     | `STATUS:OFF` | Turn lights off                                               |
| `STATUS:<state>` | `STATUS:FAULT`| Force state string                                           |

---

## Auto-Thresholds

| Condition                   | Resulting State |
|-----------------------------|-----------------|
| `input_v < 240`             | `OFF`           |
| `input_v >= 240`, was `OFF` | `ON`            |

---

## Build & Flash

```bash
# Side A
pio run -e lighting_a --target upload

# Side B
pio run -e lighting_b --target upload

# Build only (no flash)
pio run -e lighting_a
pio run -e lighting_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/lighting_a/status" -v

# Switch lights off
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "STATUS:OFF"

# Set dimmer to 50% and restore power
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "INPUT:277 DIM:50"
