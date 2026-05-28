# HV Switchgear — `hv_switchgear_a` / `hv_switchgear_b`

## Real-World Role

The HV switchgear is the data center's 230 kV main breaker — the first piece of customer equipment in the power chain and the boundary at which protection coordination hands off from the utility to the site. It sits between the utility entrance and the HV/MV step-down transformer, isolating the entire downstream chain on demand or in response to fault current. In real installations these are large SF6 gas-insulated or vacuum breakers rated to interrupt 40–63 kA of fault current. Tripping this device drops the entire side instantly — it is the upstream protection ahead of every other breaker in the chain.

---

## Nodes in This Topology

| node_id           | Side | Rated Voltage | Parent      | Child                 |
|-------------------|------|---------------|-------------|-----------------------|
| `hv_switchgear_a` | A    | 230 kV        | `utility_a` | `hv_mv_transformer_a` |
| `hv_switchgear_b` | B    | 230 kV        | `utility_b` | `hv_mv_transformer_b` |

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field       | Type   | Default | Description                                  |
|-------------|--------|---------|----------------------------------------------|
| `ts`        | string | HH:MM:SS| Timestamp from NTP                           |
| `breaker`   | bool   | true    | Breaker closed (true) / open (false)         |
| `current_a` | float  | 18.0    | Through-current at 230 kV (A)                |
| `load_kw`   | float  | 4140.0  | Active load passing through (kW)             |
| `load_pct`  | int    | 25      | Load as % of breaker rated capacity          |
| `state`     | string | CLOSED  | Operational state (see States below)         |
| `voltage`   | int    | 230000  | Rated voltage (V)                            |

---

## States

| State      | Meaning                                                              |
|------------|----------------------------------------------------------------------|
| `CLOSED`   | Breaker closed, current flowing, side energised                     |
| `OPEN`     | Breaker manually opened or no upstream feed                         |
| `TRIPPED`  | Auto-trip on overcurrent (>220 A) or overload (>95 %) — sticky      |
| `FAULT`    | Internal fault or overload warning (>180 A or >80 %) — sticky       |

`TRIPPED` and `FAULT` survive re-energisation — the broker's `_compute_node` honours them until cleared with an explicit `STATUS:CLOSED` control.

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example          | Effect                                    |
|------------------|------------------|-------------------------------------------|
| `CLOSE`          | `CLOSE`          | Close the breaker; state → `CLOSED`       |
| `OPEN`           | `OPEN`           | Open the breaker; state → `OPEN`          |
| `LOAD:<pct>`     | `LOAD:50`        | Set load %, recomputes current and kW     |
| `STATUS:<state>` | `STATUS:TRIPPED` | Force state (testing / fault injection)   |

The broker (`broker/main.py::_control_cmd` HV_SWITCHGEAR case) sends `CLOSE STATUS:<state>` when its parent is energised and not flagged faulted, and `OPEN STATUS:<state>` otherwise.

---

## Broker Behavior

`broker/main.py::_compute_node` HV_SWITCHGEAR case:

- If `parent.v_out > 0` and `status_msg` is not in `{OPEN, TRIPPED, FAULT}` → output 230 kV, state `CLOSED`
- Otherwise → output 0 V, state stays sticky (`TRIPPED` / `FAULT`) or falls to `OPEN`
