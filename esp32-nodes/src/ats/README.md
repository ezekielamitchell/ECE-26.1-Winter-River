# Automatic Transfer Switch (ATS) — `ats_a` / `ats_b`

## Real-World Role

The Automatic Transfer Switch is the critical decision point where utility and generator power paths converge. When the utility path fails, the ATS senses the loss and automatically transfers to the generator within milliseconds to seconds depending on its design — static ATS designs achieve under 100 ms, while electromechanical units take 2–10 seconds. Tier III/IV data centers use closed-transition (make-before-break) ATS designs that synchronise the generator before transferring, preventing any momentary gap in power. The ATS continuously monitors both sources for voltage, frequency, and phase angle, and automatically returns to utility power once the grid recovers and stabilises.

---

## Nodes in This Topology

| node_id  | Side | Rated Voltage | Primary Parent        | Secondary Parent | Child        |
|----------|------|---------------|-----------------------|------------------|--------------|
| `ats_a`  | A    | 480 V         | `mv_lv_transformer_a` | `generator_a`    | `lv_dist_a`  |
| `ats_b`  | B    | 480 V         | `mv_lv_transformer_b` | `generator_b`    | `lv_dist_b`  |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field      | Type   | Default  | Description                                    |
|------------|--------|----------|------------------------------------------------|
| `ts`       | string | HH:MM:SS | Local timestamp from NTP                       |
| `source`   | string | UTILITY  | Active power source                            |
| `input_v`  | float  | 480.0    | Input voltage from active source (V)           |
| `output_v` | float  | 480.0    | Output voltage to distribution board (V)       |
| `load_pct` | int    | 35       | Output load as % of rated capacity             |
| `state`    | string | UTILITY  | ATS state — matches active source              |
| `voltage`  | int    | 480      | Rated voltage (V)                              |

---

## States

| State       | Meaning                                                             |
|-------------|---------------------------------------------------------------------|
| `UTILITY`   | Transformer (utility) path active — normal operation               |
| `GENERATOR` | Generator path active — utility has failed or been shed            |
| `OPEN`      | Neither source present — no output voltage                         |
| `FAULT`     | Internal ATS failure — contacts welded, sensor fault, etc.         |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command            | Example            | Effect                                                              |
|--------------------|--------------------|---------------------------------------------------------------------|
| `SOURCE:UTILITY`   | `SOURCE:UTILITY`   | Transfers to utility path; `input_v` = 480 V, `output_v` = 480 V  |
| `SOURCE:GENERATOR` | `SOURCE:GENERATOR` | Transfers to generator path; `input_v` = 480 V, `output_v` = 480 V |
| `SOURCE:OPEN`      | `SOURCE:OPEN`      | Opens both contacts; `input_v` = 0 V, `output_v` = 0 V             |
| `LOAD:<pct>`       | `LOAD:70`          | Sets output load percentage                                         |
| `STATUS:<state>`   | `STATUS:FAULT`     | Forces state string directly                                        |

The simulation engine (`broker/main.py`) automatically sends `SOURCE` commands based on computed availability of each upstream path.

---

## Auto-Thresholds

| Condition                                            | Resulting State  |
|------------------------------------------------------|------------------|
| Utility path voltage present (> 48 V)                | `UTILITY`        |
| Utility path lost + generator path available         | `GENERATOR`      |
| Both paths lost                                      | `OPEN`           |
| `STATUS:FAULT` command received                      | `FAULT`          |

State transitions are driven by the simulation engine rather than firmware-only logic. The ATS node acts on `SOURCE` commands; it does not independently poll upstream node states.

---

## Build & Flash

```bash
# Side A
pio run -e ats_a --target upload

# Side B
pio run -e ats_b --target upload

# Build only (no flash)
pio run -e ats_a
pio run -e ats_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/ats_a/status" -v

# Transfer to generator path (simulate utility failure)
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_a/control" -m "SOURCE:GENERATOR"

# Open both contacts (both sources lost)
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_a/control" -m "SOURCE:OPEN"

# Restore utility path
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_a/control" -m "SOURCE:UTILITY"
