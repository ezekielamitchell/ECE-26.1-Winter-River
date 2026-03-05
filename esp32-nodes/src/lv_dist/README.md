# LV Distribution Board — `lv_dist_a` / `lv_dist_b`

## Real-World Role

The low-voltage (LV) distribution board — also called a main switchboard or LVMDB — is the 480 V hub that divides power into the IT load path (UPS, PDU, servers) and facility mechanical loads (cooling, lighting, monitoring systems). In real data centers this is typically a steel-enclosed panelboard or switchboard rated 400 A–6000 A with separate circuit breakers for each downstream load group. It is the last point of manual circuit-level isolation before loads, and its metering gives facility operators real-time visibility into total demand across all load categories. Overload protection at this board prevents cascade failure into downstream IT equipment during cooling or UPS faults.

---

## Nodes in This Topology

| node_id     | Side | Rated Voltage | Rating | Parent  | Children                                     |
|-------------|------|---------------|--------|---------|----------------------------------------------|
| `lv_dist_a` | A    | 480 V         | 384 kW | `ats_a` | `ups_a`, `cooling_a`, `lighting_a`, `monitoring_a` |
| `lv_dist_b` | B    | 480 V         | 384 kW | `ats_b` | `ups_b`, `cooling_b`, `lighting_b`, `monitoring_b` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field           | Type   | Default  | Description                                      |
|-----------------|--------|----------|--------------------------------------------------|
| `ts`            | string | HH:MM:SS | Local timestamp from NTP                         |
| `input_v`       | float  | 480.0    | Input voltage from ATS (V)                       |
| `ups_load_kw`   | float  | 95.0     | IT path (UPS branch) load (kW)                   |
| `mech_load_kw`  | float  | 42.0     | Mechanical load (cooling, etc.) (kW)             |
| `total_load_kw` | float  | 137.0    | Sum of all branch loads (kW)                     |
| `load_pct`      | int    | 36       | Total load as % of 384 kW rated capacity         |
| `source`        | string | UTILITY  | Upstream power source label (from ATS)           |
| `state`         | string | NORMAL   | Distribution board state (see States below)      |
| `voltage`       | float  | 480.0    | Rated voltage (V)                                |

---

## States

| State      | Meaning                                                                 |
|------------|-------------------------------------------------------------------------|
| `NORMAL`   | Healthy — load and input voltage within rated limits                    |
| `OVERLOAD` | Total load exceeds 95% of rated 384 kW — shedding risk imminent        |
| `FAULT`    | Load exceeds 85% of rated capacity — protection alarm active            |
| `NO_INPUT` | Input voltage below threshold — upstream ATS or path has failed         |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example            | Effect                                                                    |
|------------------|--------------------|---------------------------------------------------------------------------|
| `INPUT:<v>`      | `INPUT:480`        | Sets input voltage (V); below 48 V triggers `NO_INPUT`                   |
| `UPS:<kw>`       | `UPS:110.0`        | Sets UPS branch load (kW); recalculates `total_load_kw` and `load_pct`   |
| `MECH:<kw>`      | `MECH:50.0`        | Sets mechanical load (kW); recalculates `total_load_kw` and `load_pct`   |
| `SOURCE:<src>`   | `SOURCE:GENERATOR` | Sets upstream source label (UTILITY, GENERATOR, NONE) — display only     |
| `STATUS:<state>` | `STATUS:FAULT`     | Forces state string directly                                              |

---

## Auto-Thresholds

| Condition                                       | Resulting State |
|-------------------------------------------------|-----------------|
| `input_v` < 48 V                                | `NO_INPUT`      |
| `load_pct` > 95%                                | `OVERLOAD`      |
| `load_pct` > 85%                                | `FAULT`         |
| `input_v` restored and state was `NO_INPUT`     | `NORMAL`        |
| All conditions within limits                    | `NORMAL`        |

---

## Build & Flash

```bash
# Side A
pio run -e lv_dist_a --target upload

# Side B
pio run -e lv_dist_b --target upload

# Build only (no flash)
pio run -e lv_dist_a
pio run -e lv_dist_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/lv_dist_a/status" -v

# Simulate loss of input power (upstream ATS opened)
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_dist_a/control" -m "INPUT:0"

# Simulate overload condition (IT load spike)
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_dist_a/control" -m "UPS:300.0"

# Restore normal operation on generator path
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_dist_a/control" -m "INPUT:480"
