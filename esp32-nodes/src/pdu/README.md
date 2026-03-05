# Power Distribution Unit (PDU) — `pdu_a` / `pdu_b`

## Real-World Role

The rack-level PDU distributes conditioned AC power from the UPS to individual server outlets. In modern data centers, intelligent PDUs (iPDUs) provide per-outlet metering, remote switching, and environmental monitoring. Floor-mount "whip" PDUs connect to a UPS output, while horizontal rack-mount PDUs plug directly into the floor PDU. Overload protection — via individual circuit breakers or electronic current limiting — prevents a single misbehaving load from tripping an entire branch circuit, and PDU-level metering is critical for PUE (Power Usage Effectiveness) calculations.

---

## Nodes in This Topology

| node_id | Side | Rated Voltage | Parent  | Child         |
|---------|------|---------------|---------|---------------|
| `pdu_a` | A    | 480 V AC      | `ups_a` | `rectifier_a` |
| `pdu_b` | B    | 480 V AC      | `ups_b` | `rectifier_b` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field      | Type   | Default  | Description                      |
|------------|--------|----------|----------------------------------|
| `ts`       | string | HH:MM:SS | Timestamp from NTP               |
| `input_v`  | float  | 480.0    | AC input voltage (V)             |
| `output_v` | float  | 480.0    | AC output voltage to outlets (V) |
| `load_pct` | int    | 25       | Aggregate outlet load (%)        |
| `state`    | string | NORMAL   | PDU health state                 |
| `voltage`  | int    | 480      | Rated voltage (V)                |

---

## States

| State      | Meaning                                  |
|------------|------------------------------------------|
| `NORMAL`   | Input present, load within ratings       |
| `OVERLOAD` | Load exceeds 95% of rated capacity       |
| `FAULT`    | Load exceeds 85% of rated — thermal risk |
| `OFF`      | No input power                           |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example        | Effect                                        |
|------------------|----------------|-----------------------------------------------|
| `INPUT:<v>`      | `INPUT:0`      | Set input voltage; < 48V transitions to OFF   |
| `LOAD:<pct>`     | `LOAD:90`      | Set aggregate outlet load percentage          |
| `STATUS:<state>` | `STATUS:FAULT` | Force state string                            |

---

## Auto-Thresholds

| Condition                  | Resulting State |
|----------------------------|-----------------|
| `input_v < 48`             | `OFF`           |
| `load_pct > 95`            | `OVERLOAD`      |
| `load_pct > 85`            | `FAULT`         |
| Input restored, was `OFF`  | `NORMAL`        |

---

## Build & Flash

```bash
# Side A
pio run -e pdu_a --target upload

# Side B
pio run -e pdu_b --target upload

# Build only (no flash)
pio run -e pdu_a
pio run -e pdu_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/pdu_a/status" -v

# Simulate upstream UPS failure (cuts input)
mosquitto_pub -h 192.168.4.1 -t "winter-river/pdu_a/control" -m "INPUT:0"

# Inject an overload condition
mosquitto_pub -h 192.168.4.1 -t "winter-river/pdu_a/control" -m "LOAD:98"

# Restore normal operation
mosquitto_pub -h 192.168.4.1 -t "winter-river/pdu_a/control" -m "INPUT:480"
