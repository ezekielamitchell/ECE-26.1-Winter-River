# Monitoring Equipment — `monitoring_a` / `monitoring_b`

## Real-World Role

Data center infrastructure management (DCIM) systems, building management systems (BMS), environmental sensors, CCTV, and network management platforms all require reliable 120V AC power from a utility-derived step-down transformer. These systems must remain operational even during partial power events because they provide the visibility operators need to diagnose and respond to failures in real time. Many facilities place monitoring equipment on a dedicated small UPS or a separate branch of the main UPS to ensure maximum uptime. Loss of monitoring is itself a declared incident in Tier III/IV operations — operators working without instrumentation during an outage face unacceptable safety and recovery risk.

---

## Nodes in This Topology

| node_id        | Side | Rated Voltage | Parent      | Children |
|----------------|------|---------------|-------------|----------|
| `monitoring_a` | A    | 120 V AC      | `lv_dist_a` | none     |
| `monitoring_b` | B    | 120 V AC      | `lv_dist_b` | none     |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field      | Type   | Default  | Description                       |
|------------|--------|----------|-----------------------------------|
| `ts`       | string | HH:MM:SS | Timestamp from NTP                |
| `input_v`  | float  | 120.0    | Supply voltage (V)                |
| `load_pct` | int    | 15       | Load as % of rated capacity       |
| `state`    | string | NORMAL   | Monitoring system state           |
| `voltage`  | int    | 120      | Rated voltage (V)                 |

---

## States

| State    | Meaning                                                 |
|----------|---------------------------------------------------------|
| `NORMAL` | Powered — all monitoring systems operating              |
| `FAULT`  | Overvoltage (> 145V) or other electrical anomaly        |
| `OFF`    | No input power (< 100V) — monitoring systems dark       |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example          | Effect                                          |
|------------------|------------------|-------------------------------------------------|
| `INPUT:<v>`      | `INPUT:0`        | Set input voltage; < 100V → OFF, > 145V → FAULT |
| `LOAD:<pct>`     | `LOAD:30`        | Set load percentage                             |
| `STATUS:<state>` | `STATUS:FAULT`   | Force state string                              |

---

## Auto-Thresholds

| Condition                     | Resulting State |
|-------------------------------|-----------------|
| `input_v < 100`               | `OFF`           |
| `input_v > 145`               | `FAULT`         |
| `input_v` restored, was `OFF` | `NORMAL`        |

---

## Build & Flash

```bash
# Side A
pio run -e monitoring_a --target upload

# Side B
pio run -e monitoring_b --target upload

# Build only (no flash)
pio run -e monitoring_a
pio run -e monitoring_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/monitoring_a/status" -v

# Simulate power loss to monitoring (triggers OFF)
mosquitto_pub -h 192.168.4.1 -t "winter-river/monitoring_a/control" -m "INPUT:0"

# Restore power to monitoring
mosquitto_pub -h 192.168.4.1 -t "winter-river/monitoring_a/control" -m "INPUT:120"
