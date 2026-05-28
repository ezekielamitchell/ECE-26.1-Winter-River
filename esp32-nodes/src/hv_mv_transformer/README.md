# HV/MV Transformer — `hv_mv_transformer_a` / `hv_mv_transformer_b`

## Real-World Role

The HV/MV transformer is the substation-class step-down at the data center's
utility entrance, dropping the 230 kV transmission line down to a 34.5 kV
medium-voltage bus that the MV switchgear can safely interrupt and meter.
Utility power enters as raw transmission voltage; nothing in the facility
operates at that level, so this transformer is the first piece of customer
equipment in the chain and the boundary at which protection coordination
hands off from the utility to the data center operator. Failure here is
catastrophic and takes the entire side offline until repaired.

This node was added to the topology so the broker's voltage-propagation model
explicitly tracks the 230 kV → 34.5 kV step-down as its own stage instead of
folding it into `mv_switchgear` as a ratio.

---

## Nodes in This Topology

| node_id                  | Side | Input Voltage | Output Voltage | Parent              | Child             |
|--------------------------|------|---------------|----------------|---------------------|-------------------|
| `hv_mv_transformer_a`    | A    | 230 kV        | 34.5 kV        | `hv_switchgear_a`   | `mv_switchgear_a` |
| `hv_mv_transformer_b`    | B    | 230 kV        | 34.5 kV        | `hv_switchgear_b`   | `mv_switchgear_b` |

---

## Broker Behavior

The broker (`broker/main.py`) treats this node like the existing
`MV_LV_TRANSFORMER` handler: pass parent voltage through (stepped down to
34.5 kV) when the node's status is `NORMAL` or `WARNING`; produce 0 V on
`FAULT`. The control payload is `STATUS:<state>`.

| State     | Meaning                                                       |
|-----------|---------------------------------------------------------------|
| `NORMAL`  | Healthy step-down operation                                   |
| `WARNING` | Elevated temperature or load — still passing voltage          |
| `FAULT`   | Transformer de-energised; downstream MV switchgear loses feed |

---

## Status

> **Firmware pending.** This folder is a placeholder for the eventual ESP32
> firmware. The simulation engine, PostgreSQL seed, and `scripts/status.sh`
> already include `hv_mv_transformer_{a,b}`, so any new firmware should adopt
> the shared `wr::begin()` pattern (see `esp32-nodes/lib/winter_river/`) and
> publish a `NORMAL` / `WARNING` / `FAULT` status field.
