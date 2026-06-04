# LV Switchgear — `lv_switchgear_a` / `lv_switchgear_b`

## Real-World Role

The LV switchgear is the low-voltage protection, isolation, and **source-transfer**
stage on the **480 V bus**, immediately downstream of the MV/LV step-down
transformer. It is the utility↔generator transfer point for the side — the role a
separate automatic transfer switch (ATS) used to play in this model. Its
**primary** input is the MV/LV-transformer (utility-derived) path; its
**secondary** input is the standby generator. It prefers the utility path and
transfers to the generator when the LV bus loses its transformer feed. Its output
energises the side's UPS (IT path) and cooling (mech path) in parallel — so
opening or tripping it drops the whole side. In real installations this is a
low-voltage switchboard with a main breaker, a generator tie breaker, branch
breakers, and metering.

> **Naming:** switchgear is named for the bus voltage it sits on. `mv_switchgear`
> is on the 34.5 kV **MV** bus (downstream of the HV/MV transformer);
> `lv_switchgear` is on the 480 V **LV** bus (downstream of the MV/LV
> transformer). There is no switchgear on the 230 kV side in this topology.

---

## Nodes in This Topology

| node_id           | Side | Rated Voltage | Parent (primary)      | Secondary parent | Children              |
|-------------------|------|---------------|-----------------------|------------------|-----------------------|
| `lv_switchgear_a` | A    | 480 V         | `mv_lv_transformer_a` | `generator_a`    | `ups_a`, `cooling_a`  |
| `lv_switchgear_b` | B    | 480 V         | `mv_lv_transformer_b` | `generator_b`    | `ups_b`, `cooling_b`  |

Chain context (per side):
`utility → hv_mv_transformer → mv_switchgear → mv_lv_transformer → `**`lv_switchgear`**` → ups → server_rack_{1..4}`
with `generator ↗ lv_switchgear` (secondary feed) and `lv_switchgear ↘ cooling`
(mech branch, parallel to the UPS).

The LV switchgear is the side's transfer point: it prefers the
MV/LV-transformer (utility) path and falls back to `generator_*` when the LV bus
loses its transformer feed. There is no separate ATS node.

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

| State       | Meaning                                                                 |
|-------------|--------------------------------------------------------------------------|
| `CLOSED`    | Normal — main breaker closed on the utility (MV/LV transformer) path; 480 V LV bus energised |
| `GENERATOR` | Transferred to the standby generator (utility path lost, generator running) — bus still 480 V |
| `NO_INPUT`  | Both sources dead (no transformer feed, generator not running) — **not sticky** |
| `OPEN`      | Main breaker opened by the operator — sticky; drops the whole side       |
| `TRIPPED`   | Protective relay triggered a fault trip — sticky                         |
| `FAULT`     | Overcurrent / overload detected — sticky                                 |

`OPEN`, `TRIPPED`, and `FAULT` are sticky — they hold the bus dead (blocking
**both** sources) until cleared with an explicit `CLOSE` / `STATUS:CLOSED`
control. `NO_INPUT` is **not** sticky: it is just the "no live source" label, so
the bus re-energises the moment the utility path or the generator returns. (That
is what carries a utility-outage → generator-transfer → utility-recovery cascade
end to end.)

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

`broker/main.py::_compute_node` LV_SWITCHGEAR case (the transfer logic):

- `status_msg` in `{OPEN, TRIPPED, FAULT}` (sticky) → output 0 V, hold that state
- else `parent.v_out > 0` (utility path live) → output 480 V, state `CLOSED`
- else `secondary_parent.v_out > 0` (generator running) → output 480 V, state `GENERATOR`
- else (both sources dead) → output 0 V, state `NO_INPUT` (non-sticky)

The control command is `CLOSE STATUS:<state>` when energised and
`OPEN STATUS:<state>` when not. Because the generator now ties in behind this
switchgear, the clean way to demo a transfer is to drop the **utility** upstream
(`utility_a STATUS:OUTAGE`) and watch the bus go `CLOSED → NO_INPUT → GENERATOR`.

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

# Demo the generator transfer by dropping the utility upstream; the broker
# transfers the LV bus to the generator (CLOSED → NO_INPUT → GENERATOR), then
# back to the utility path (GENERATOR → CLOSED) on recovery.
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK"

# Open the LV main breaker — drops the WHOLE side (ups + cooling), generator
# included, since the generator ties in behind this switchgear. Sticky.
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "OPEN"

# Simulate a fault trip (sticky)
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "STATUS:TRIPPED"

# Restore and re-close the breaker
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "CLOSE"
```
