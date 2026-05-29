# LV Switchgear — `lv_switchgear_a` / `lv_switchgear_b`

## Real-World Role

The LV switchgear is the low-voltage protection and isolation stage on the
**480 V bus**, immediately downstream of the MV/LV step-down transformer. It is
the last switchable point before the automatic transfer switch (ATS): its output
is the ATS *primary* (utility-derived) input, with the standby generator feeding
the ATS secondary. Opening or tripping the LV switchgear forces the ATS to fall
back to the generator (if running) or to drop the side. In real installations
this is a low-voltage switchboard with a main breaker, branch breakers, and
metering feeding the UPS and mechanical loads.

> **Naming:** switchgear is named for the bus voltage it sits on. `mv_switchgear`
> is on the 34.5 kV **MV** bus (downstream of the HV/MV transformer);
> `lv_switchgear` is on the 480 V **LV** bus (downstream of the MV/LV
> transformer). There is no switchgear on the 230 kV side in this topology.

---

## Nodes in This Topology

| node_id           | Side | Rated Voltage | Parent                  | Child   |
|-------------------|------|---------------|-------------------------|---------|
| `lv_switchgear_a` | A    | 480 V         | `mv_lv_transformer_a`   | `ats_a` |
| `lv_switchgear_b` | B    | 480 V         | `mv_lv_transformer_b`   | `ats_b` |

Chain context (per side):
`utility → hv_mv_transformer → mv_switchgear → mv_lv_transformer → `**`lv_switchgear`**` → ats`

The LV switchgear output is the **ATS primary input**. The generator feeds the
ATS secondary; `ats_*` prefers the LV switchgear path and falls back to the
generator when the LV bus is dead.

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field       | Type   | Default  | Description                              |
|-------------|--------|----------|------------------------------------------|
| `ts`        | string | HH:MM:SS | Local timestamp from NTP                 |
| `breaker`   | bool   | true     | Main breaker state (true = closed)       |
| `current_a` | float  | 625.0    | Line current at 480 V (A)                |
| `load_kw`   | float  | 300.0    | Active power (kW)                        |
| `load_pct`  | int    | 30       | Load as % of rated capacity              |
| `state`     | string | CLOSED   | Switchgear state (see States below)      |
| `voltage`   | int    | 480      | Rated voltage (V)                        |

---

## States

| State     | Meaning                                                        |
|-----------|----------------------------------------------------------------|
| `CLOSED`  | Normal — main breaker closed, 480 V LV bus energised, feeding the ATS primary |
| `OPEN`    | Main breaker opened, or no upstream feed (forces ATS to generator) |
| `TRIPPED` | Protective relay triggered a fault trip — sticky               |
| `FAULT`   | Overcurrent / overload detected — sticky                       |

`TRIPPED` and `FAULT` survive re-energisation — the broker's `_compute_node`
honours them until cleared with an explicit `STATUS:CLOSED` control.

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                              |
|------------------|-------------------|---------------------------------------------------------------------|
| `CLOSE`          | `CLOSE`           | Closes main breaker; state → `CLOSED`                              |
| `OPEN`           | `OPEN`            | Opens main breaker; state → `OPEN`                                 |
| `LOAD:<pct>`     | `LOAD:60`         | Sets load %; recalculates `load_kw` and `current_a` proportionally |
| `STATUS:<state>` | `STATUS:TRIPPED`  | Forces state string                                                 |

The broker (`broker/main.py::_control_cmd` LV_SWITCHGEAR case) sends
`CLOSE STATUS:<state>` when its parent transformer is energised and not flagged
faulted, and `OPEN STATUS:<state>` otherwise.

---

## Auto-Thresholds

| Condition                                    | Resulting State           |
|----------------------------------------------|---------------------------|
| `load_pct` > 95% or `current_a` > 2000 A    | `TRIPPED` + breaker opens |
| `load_pct` > 80% or `current_a` > 1670 A    | `FAULT`                   |
| Breaker closed and load within range         | `CLOSED`                  |

---

## Broker Behavior

`broker/main.py::_compute_node` LV_SWITCHGEAR case:

- If `parent.v_out > 0` and `status_msg` is not in `{OPEN, TRIPPED, FAULT}` → output 480 V, state `CLOSED`
- Otherwise → output 0 V, state stays sticky (`TRIPPED` / `FAULT`) or falls to `OPEN`

Because this node feeds the ATS primary, opening it is the clean way to demo an
ATS transfer to generator without faulting the upstream chain.

---

## Build & Flash

```bash
# Side A
pio run -e lv_switchgear_a --target upload

# Side B
pio run -e lv_switchgear_b --target upload

# Build only (no flash)
pio run -e lv_switchgear_a
pio run -e lv_switchgear_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/status" -v

# Open the LV breaker (forces ats_a to transfer to the generator)
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "OPEN"

# Simulate a fault trip
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "STATUS:TRIPPED"

# Restore and re-close breaker
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "CLOSE"
```
