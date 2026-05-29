# MV/LV Transformer — `mv_lv_transformer_a` / `mv_lv_transformer_b`

## Real-World Role

The MV/LV transformer steps down medium-voltage grid power to the 480 V building distribution level used by downstream power conditioning equipment. In hyperscale and enterprise facilities these are typically dry-type or liquid-filled units rated 500 kVA–5 MVA. Transformer health is monitored via winding temperature sensors because insulation degradation is the primary failure mode — most manufacturers rate winding insulation up to 150°C but set alarm thresholds at 120–140°C to preserve service life. A failed transformer takes the entire utility power path offline until the unit is repaired or a bypass is established.

---

## Nodes in This Topology

| node_id                | Side | Input Voltage | Output Voltage | Rating    | Parent            | Child     |
|------------------------|------|---------------|----------------|-----------|-------------------|-----------|
| `mv_lv_transformer_a`  | A    | 34.5 kV       | 480 V          | 1000 kVA  | `mv_switchgear_a` | `lv_switchgear_a` |
| `mv_lv_transformer_b`  | B    | 34.5 kV       | 480 V          | 1000 kVA  | `mv_switchgear_b` | `lv_switchgear_b` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field       | Type   | Default  | Description                                    |
|-------------|--------|----------|------------------------------------------------|
| `ts`        | string | HH:MM:SS | Local timestamp from NTP                       |
| `load_pct`  | int    | 45       | Load as % of rated kVA                         |
| `power_kva` | float  | 450.0    | Apparent power output (kVA)                    |
| `temp_f`    | int    | 112      | Winding temperature (°F)                       |
| `state`     | string | NORMAL   | Transformer health state (see States below)    |
| `voltage`   | int    | 480      | Output voltage (V)                             |

---

## States

| State     | Meaning                                                              |
|-----------|----------------------------------------------------------------------|
| `NORMAL`  | Healthy — load and temperature within rated limits                   |
| `WARNING` | Elevated load (> 75%) or elevated temperature (> 149°F) — watch     |
| `FAULT`   | Critical — transformer de-energised or protection relay triggered    |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example          | Effect                                                          |
|------------------|------------------|-----------------------------------------------------------------|
| `LOAD:<pct>`     | `LOAD:80`        | Sets load %; auto-calculates `power_kva` = (pct / 100) × 1000  |
| `TEMP:<f>`       | `TEMP:160`       | Sets winding temperature in °F; may trigger WARNING or FAULT    |
| `STATUS:<state>` | `STATUS:WARNING` | Forces state string                                             |

---

## Auto-Thresholds

| Condition                           | Resulting State |
|-------------------------------------|-----------------|
| `load_pct` > 90% or `temp_f` > 185°F | `FAULT`       |
| `load_pct` > 75% or `temp_f` > 149°F | `WARNING`     |
| Both conditions within limits       | `NORMAL`        |

---

## Build & Flash

```bash
# Side A
pio run -e mv_lv_transformer_a --target upload

# Side B
pio run -e mv_lv_transformer_b --target upload

# Build only (no flash)
pio run -e mv_lv_transformer_a
pio run -e mv_lv_transformer_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/status" -v

# Simulate overload condition
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:92"

# Simulate thermal fault
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "TEMP:195"

# Restore to normal
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:45"
