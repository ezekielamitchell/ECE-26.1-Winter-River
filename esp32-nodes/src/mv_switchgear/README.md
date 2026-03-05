# MV Switchgear — `mv_switchgear_a` / `mv_switchgear_b`

## Real-World Role

Medium-voltage (MV) switchgear is the primary electrical isolation and protection equipment at a data center's utility entrance. It houses the main disconnect switch, metering CTs/PTs, and protective relays (overcurrent, ground fault, differential protection). In commercial data centers, MV switchgear is typically rated 5 kV–38 kV and provides the first line of defense against fault propagation from the utility. Operators can open the main breaker to isolate the facility from the grid during maintenance or grid-side faults without interrupting generator-backed downstream loads.

---

## Nodes in This Topology

| node_id           | Side | Rated Voltage | Parent      | Child                  |
|-------------------|------|---------------|-------------|------------------------|
| `mv_switchgear_a` | A    | 34.5 kV       | `utility_a` | `mv_lv_transformer_a`  |
| `mv_switchgear_b` | B    | 34.5 kV       | `utility_b` | `mv_lv_transformer_b`  |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field       | Type   | Default  | Description                              |
|-------------|--------|----------|------------------------------------------|
| `ts`        | string | HH:MM:SS | Local timestamp from NTP                 |
| `breaker`   | bool   | true     | Main breaker state (true = closed)       |
| `current_a` | float  | 15.2     | Line current (A)                         |
| `load_kw`   | float  | 420.0    | Active power (kW)                        |
| `load_pct`  | int    | 30       | Load as % of rated capacity              |
| `state`     | string | CLOSED   | Switchgear state (see States below)      |
| `voltage`   | int    | 34500    | Rated voltage (V)                        |

---

## States

| State     | Meaning                                                        |
|-----------|----------------------------------------------------------------|
| `CLOSED`  | Normal — main breaker closed, facility energised from utility  |
| `OPEN`    | Main breaker manually or automatically opened                  |
| `TRIPPED` | Protective relay triggered a fault trip                        |
| `FAULT`   | Overcurrent or overvoltage detected — alarm active             |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                              |
|------------------|-------------------|---------------------------------------------------------------------|
| `CLOSE`          | `CLOSE`           | Closes main breaker; state → `CLOSED`                              |
| `OPEN`           | `OPEN`            | Opens main breaker; state → `OPEN`                                 |
| `LOAD:<pct>`     | `LOAD:60`         | Sets load %; recalculates `load_kw` and `current_a` proportionally |
| `STATUS:<state>` | `STATUS:TRIPPED`  | Forces state string                                                 |

---

## Auto-Thresholds

| Condition                                   | Resulting State           |
|---------------------------------------------|---------------------------|
| `load_pct` > 95% or `current_a` > 40 A     | `TRIPPED` + breaker opens |
| `load_pct` > 80% or `current_a` > 32 A     | `FAULT`                   |
| Breaker closed and load within range        | `CLOSED`                  |

---

## Build & Flash

```bash
# Side A
pio run -e mv_switchgear_a --target upload

# Side B
pio run -e mv_switchgear_b --target upload

# Build only (no flash)
pio run -e mv_switchgear_a
pio run -e mv_switchgear_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/status" -v

# Open the main breaker (isolate facility from grid)
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "OPEN"

# Simulate a fault trip
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "STATUS:TRIPPED"

# Restore and re-close breaker
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE"
```
