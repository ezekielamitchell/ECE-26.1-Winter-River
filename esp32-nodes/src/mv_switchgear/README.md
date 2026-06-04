# MV Switchgear — `mv_switchgear_a` / `mv_switchgear_b`

## Real-World Role

The MV switchgear is the medium-voltage protection and isolation stage that sits
on the **34.5 kV bus**, immediately downstream of the HV/MV step-down
transformer. It houses the main MV breaker, metering CTs/PTs, and protective
relays (overcurrent, ground fault, differential). In commercial data centers,
MV switchgear is typically rated 5 kV–38 kV and provides the first switchable
point of isolation on-site: operators can open it to drop the entire downstream
chain for maintenance or in response to a fault, without touching the utility
service entrance. Tripping it de-energises everything from the MV/LV transformer
down on that side.

> **Naming:** switchgear is named for the bus voltage it sits on. `mv_switchgear`
> is on the 34.5 kV **MV** bus (downstream of the HV/MV transformer);
> `lv_switchgear` is on the 480 V **LV** bus (downstream of the MV/LV
> transformer). There is no switchgear on the 230 kV side in this topology.

---

## Nodes in This Topology

| node_id           | Side | Rated Voltage | Parent                  | Child                 |
|-------------------|------|---------------|-------------------------|-----------------------|
| `mv_switchgear_a` | A    | 34.5 kV       | `hv_mv_transformer_a`   | `mv_lv_transformer_a` |
| `mv_switchgear_b` | B    | 34.5 kV       | `hv_mv_transformer_b`   | `mv_lv_transformer_b` |

Chain context (per side):
`utility → hv_mv_transformer → `**`mv_switchgear`**` → mv_lv_transformer → lv_switchgear → ups → server_rack_{1..4}`

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field       | Type   | Default  | Description                              |
|-------------|--------|----------|------------------------------------------|
| `ts`        | string | HH:MM:SS | Local timestamp from NTP                 |
| `breaker`   | bool   | true     | Main breaker state (true = closed)       |
| `current_a` | float  | 116.0    | Line current at 34.5 kV (A)              |
| `load_kw`   | float  | 4000.0   | Active power (kW)                        |
| `load_pct`  | int    | 25       | Load as % of rated capacity              |
| `state`     | string | CLOSED   | Switchgear state (see States below)      |
| `voltage`   | int    | 34500    | Rated voltage (V)                        |

---

## States

| State      | Meaning                                                        |
|------------|----------------------------------------------------------------|
| `CLOSED`   | Normal — main breaker closed, MV bus energised from the HV/MV transformer |
| `NO_INPUT` | No upstream feed (HV/MV transformer dead) — **not sticky**, re-closes when re-energised |
| `OPEN`     | Main breaker opened by the operator — sticky                   |
| `TRIPPED`  | Protective relay triggered a fault trip — sticky               |
| `FAULT`    | Overcurrent / overload detected — sticky                       |

`OPEN`, `TRIPPED`, and `FAULT` are sticky — they survive re-energisation until
cleared with an explicit `CLOSE` / `STATUS:CLOSED` control. `NO_INPUT` is the
non-sticky "unfed" label, so the breaker re-closes automatically once the
upstream chain comes back (this is what lets a utility outage recover cleanly).

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                              |
|------------------|-------------------|---------------------------------------------------------------------|
| `CLOSE`          | `CLOSE`           | Closes main breaker; state → `CLOSED`                              |
| `OPEN`           | `OPEN`            | Opens main breaker; state → `OPEN`                                 |
| `LOAD:<pct>`     | `LOAD:60`         | Sets load %; recalculates `load_kw` and `current_a` proportionally |
| `STATUS:<state>` | `STATUS:TRIPPED`  | Forces state string                                                 |

The broker (`broker/main.py::_control_cmd` MV_SWITCHGEAR case) sends
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

`broker/main.py::_compute_node` MV_SWITCHGEAR case:

- `status_msg` in `{OPEN, TRIPPED, FAULT}` (sticky) → output 0 V, hold that state
- else `parent.v_out > 0` → output 34.5 kV, state `CLOSED`
- else (unfed) → output 0 V, state `NO_INPUT` (non-sticky, re-closes when re-fed)

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

# Open the MV breaker (isolate the downstream chain on Side A)
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "OPEN"

# Simulate a fault trip
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "STATUS:TRIPPED"

# Restore and re-close breaker
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE"
```
