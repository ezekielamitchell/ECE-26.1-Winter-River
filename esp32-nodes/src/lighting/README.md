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

| Field            | Type   | Default  | Description                            |
|------------------|--------|----------|----------------------------------------|
| `ts`             | string | HH:MM:SS | Timestamp from NTP                     |
| `input_v`        | float  | 277.0    | Supply voltage (V)                     |
| `zones_active`   | int    | 4        | Number of energised lighting zones     |
| `brightness_pct` | int    | 100      | Brightness / occupancy control setting |
| `load_pct`       | int    | 40       | Electrical load as % of rated capacity |
| `state`          | string | NORMAL   | Lighting circuit state                 |
| `voltage`        | int    | 277      | Rated voltage (V)                      |

---

## States

| State      | Meaning                                          |
|------------|--------------------------------------------------|
| `NORMAL`   | Circuit powered and all lighting zones active    |
| `DIMMED`   | Circuit powered but brightness reduced           |
| `OFF`      | No input power or circuit switched off           |
| `FAULT`    | Input voltage anomaly detected                   |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example      | Effect                                                        |
|------------------|--------------|---------------------------------------------------------------|
| `INPUT:<v>`      | `INPUT:277`         | Set input voltage; < 240V transitions to OFF                  |
| `DIM:<pct>`      | `DIM:50`            | Set brightness %; drives `brightness_pct` and `load_pct`      |
| `STATUS:<state>` | `STATUS:DIMMED`     | Force state string directly                                   |

---

## Auto-Thresholds

| Condition                          | Resulting State |
|------------------------------------|-----------------|
| `input_v < 240` or `brightness=0`  | `OFF`           |
| `input_v > 305`                    | `FAULT`         |
| `brightness_pct < 100`             | `DIMMED`        |
| `input_v` nominal and brightness 100 | `NORMAL`      |

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

# Dim lights to 50%
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "DIM:50"

# Restore full brightness
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "INPUT:277 DIM:100"
