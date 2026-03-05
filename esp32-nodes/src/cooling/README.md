# Cooling System — `cooling_a` / `cooling_b`

## Real-World Role

Computer Room Air Conditioning (CRAC) and Computer Room Air Handling (CRAH) units are the primary mechanical cooling loads in a data center, typically consuming 30–40% of total facility power. Modern facilities target supply air temperatures of 64–77°F (18–25°C) at the server inlet per ASHRAE A1/A2 thermal guidelines. Cooling failures are among the most time-critical incidents in data center operations — without active cooling, server inlet temperatures can exceed thermal shutdown thresholds within minutes at high rack density. Redundant cooling (N+1 or 2N) with automatic failover between units is standard practice in Tier III/IV facilities.

---

## Nodes in This Topology

| node_id     | Side | Rated Voltage | Parent      | Children |
|-------------|------|---------------|-------------|----------|
| `cooling_a` | A    | 480 V AC      | `lv_dist_a` | none     |
| `cooling_b` | B    | 480 V AC      | `lv_dist_b` | none     |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field            | Type   | Default  | Description                             |
|------------------|--------|----------|-----------------------------------------|
| `ts`             | string | HH:MM:SS | Timestamp from NTP                      |
| `input_v`        | float  | 480.0    | AC input voltage (V)                    |
| `coolant_temp_f` | int    | 65       | Supply coolant/air temperature (°F)     |
| `fan_speed_pct`  | int    | 60       | Fan or pump speed as % of rated         |
| `load_pct`       | int    | 60       | Power load % (tracks fan_speed_pct)     |
| `state`          | string | NORMAL   | Cooling unit state                      |
| `voltage`        | int    | 480      | Rated voltage (V)                       |

---

## States

| State      | Meaning                                                       |
|------------|---------------------------------------------------------------|
| `NORMAL`   | Operating within design parameters; supply temp in range      |
| `DEGRADED` | Supply temperature elevated but unit functional               |
| `FAULT`    | Supply temperature critical — active risk to server hardware  |
| `OFF`      | No input power; unit not operating                            |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                         |
|------------------|-------------------|----------------------------------------------------------------|
| `INPUT:<v>`      | `INPUT:0`         | Set input voltage; < 48V transitions to OFF                    |
| `TEMP:<f>`       | `TEMP:85`         | Set supply temperature; > 80°F → FAULT, > 72°F → DEGRADED     |
| `SPEED:<pct>`    | `SPEED:40`        | Set fan/pump speed %; also sets load_pct                       |
| `STATUS:<state>` | `STATUS:DEGRADED` | Force state string                                             |

---

## Auto-Thresholds

| Condition              | Resulting State |
|------------------------|-----------------|
| `input_v < 48`         | `OFF`           |
| `coolant_temp_f > 80`  | `FAULT`         |
| `coolant_temp_f > 72`  | `DEGRADED`      |
| `coolant_temp_f <= 72` | `NORMAL`        |

---

## Build & Flash

```bash
# Side A
pio run -e cooling_a --target upload

# Side B
pio run -e cooling_b --target upload

# Build only (no flash)
pio run -e cooling_a
pio run -e cooling_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/cooling_a/status" -v

# Simulate a cooling failure (supply temperature spike)
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "TEMP:88"

# Restore nominal cooling
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "TEMP:65"
