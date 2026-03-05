# Utility Grid — `utility_a` / `utility_b`

## Real-World Role

The utility feed is the primary power source for a data center — high-voltage AC power delivered from the regional electrical grid (typically 115 kV–230 kV in North America) through a substation to the facility's point of common coupling (PCC). Utility power is the cheapest and most stable source under normal operating conditions, and reliability targets for Tier III/IV data centers require utility uptime of 99.982% or greater. Any sag, swell, or outage at this point cascades through the entire downstream power chain. Metering at this boundary is also used for utility billing and power quality compliance reporting.

---

## Nodes in This Topology

| node_id     | Side | Rated Voltage | Parent | Child             |
|-------------|------|---------------|--------|-------------------|
| `utility_a` | A    | 230 kV        | none   | `mv_switchgear_a` |
| `utility_b` | B    | 230 kV        | none   | `mv_switchgear_b` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field        | Type   | Default  | Description                           |
|--------------|--------|----------|---------------------------------------|
| `ts`         | string | HH:MM:SS | Local timestamp from NTP              |
| `v_out`      | float  | 230.0    | Grid output voltage (kV)              |
| `freq_hz`    | float  | 60.0     | AC frequency (Hz)                     |
| `load_pct`   | int    | 12       | Load as % of rated capacity           |
| `state`      | string | GRID_OK  | Current grid state (see States below) |
| `voltage_kv` | float  | 230.0    | Rated voltage label (kV)              |
| `phase`      | int    | 3        | Number of phases                      |

---

## States

| State     | Meaning                                                             |
|-----------|---------------------------------------------------------------------|
| `GRID_OK` | Normal operation — voltage and frequency within nominal tolerances  |
| `SAG`     | Voltage dropped to 88–90% of nominal (230 kV)                      |
| `SWELL`   | Voltage risen to 110%+ of nominal                                   |
| `OUTAGE`  | Voltage at or near 0 — no power being delivered                     |
| `FAULT`   | Frequency outside acceptable range (59.3–60.7 Hz)                  |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example         | Effect                                                                 |
|------------------|-----------------|------------------------------------------------------------------------|
| `STATUS:<state>` | `STATUS:OUTAGE` | Forces state; auto-adjusts voltage (OUTAGE → 0 kV, GRID_OK → 230 kV) |
| `VOLT:<kv>`      | `VOLT:184.0`    | Sets output voltage; derives state from ratio vs. nominal              |
| `FREQ:<hz>`      | `FREQ:58.8`     | Sets AC frequency; outside 59.3–60.7 Hz triggers FAULT                |
| `LOAD:<pct>`     | `LOAD:45`       | Sets load percentage (display only — no downstream firmware effect)    |

---

## Auto-Thresholds

| Condition                             | Resulting State |
|---------------------------------------|-----------------|
| `v_out` < 0.1 kV                      | `OUTAGE`        |
| `v_out` < 207 kV (< 90% of 230 kV)   | `SAG`           |
| `v_out` > 253 kV (> 110% of 230 kV)  | `SWELL`         |
| `freq_hz` outside 59.3–60.7 Hz       | `FAULT`         |
| All conditions nominal                | `GRID_OK`       |

---

## Build & Flash

```bash
# Side A
pio run -e utility_a --target upload

# Side B
pio run -e utility_b --target upload

# Build only (no flash)
pio run -e utility_a
pio run -e utility_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/utility_a/status" -v

# Simulate a utility outage (triggers full Side-A cascade via LWT propagation)
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE"

# Simulate a voltage sag
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "VOLT:200.0"

# Restore normal grid
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK"
```
