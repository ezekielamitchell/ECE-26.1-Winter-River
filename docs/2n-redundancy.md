# 2N Redundancy — How It Works

This document explains how the Winter River simulator implements 2N redundancy: what it means, how the two power paths are wired, how failures cascade and recover, and how to trigger test scenarios manually.

---

## What Is 2N Redundancy?

2N means **two independent, full-capacity power paths** feed every load. If one path fails completely, the other carries 100% of the load with no interruption. Real data centers use this to achieve continuous uptime even during a full side failure.

In Winter River, the load is the `server_rack`. It receives 48V DC from two completely separate chains:

- **Side A** — `utility_a → ... → rectifier_a → server_rack`
- **Side B** — `utility_b → ... → rectifier_b → server_rack`

Both paths run simultaneously. The server rack reports which paths are live.

---

## Full Dual-Path Topology

```
SIDE A                                      SIDE B
──────────────────────────────────────────────────────────────────
utility_a (230kV grid)                      utility_b (230kV grid)
    │                                           │
mv_switchgear_a (breaker)               mv_switchgear_b (breaker)
    │                                           │
mv_lv_transformer_a (→480V)         mv_lv_transformer_b (→480V)
    │                      ┌──────────────────┐ │
    └──────────┐            │    generator_a/b │ │
           ats_a ←──────────┘                └─→ ats_b
               │   (switches to generator          │
               │    if transformer fails)           │
          lv_dist_a (480V bus)              lv_dist_b (480V bus)
          ├── cooling_a                     ├── cooling_b
          ├── lighting_a                    ├── lighting_b
          ├── monitoring_a                  ├── monitoring_b
          └── ups_a                         └── ups_b
                 │                                 │
              pdu_a                             pdu_b
                 │                                 │
           rectifier_a (→48V DC)          rectifier_b (→48V DC)
                 │                                 │
                 └──────────┬────────────────────┘
                            │
                       server_rack
                    (48V DC, 2N shared)
```

Each side is a complete, independent chain. The `server_rack` is the only shared node — it deliberately receives input from both sides.

---

## Redundancy at Each Layer

There are three levels of redundancy stacked on top of each other.

### Layer 1 — Utility-to-Generator Failover (per side)

Each side has an **Automatic Transfer Switch (ATS)** with two inputs:
- Primary: the transformer path (utility grid)
- Secondary: the diesel generator (backup)

The simulation engine (`broker/main.py`) monitors `utility_a/b` status every second. When utility goes to `OUTAGE` or `FAULT`:

1. The engine sets the generator to `STARTING` and begins a **10-tick countdown** (~10 seconds).
2. During startup, the ATS has no input — `lv_dist` and everything downstream goes to `NO_INPUT`.
3. When the countdown reaches zero, the generator transitions to `RUNNING` (480V output).
4. The engine sends `SOURCE:GENERATOR` to the ATS, restoring power to the full side.

When utility recovers, the generator returns to `STANDBY` and the ATS switches back to `SOURCE:UTILITY`.

**ATS states:**

| State | Meaning |
|---|---|
| `UTILITY` | Normal — grid is supplying power |
| `GENERATOR` | Generator has taken over after utility failure |
| `OPEN` | No source available — both inputs dead |
| `FAULT` | Hardware fault on the switch itself |

### Layer 2 — Side A vs Side B Independence

The two sides share nothing except the server rack. A complete failure of Side A (utility outage + generator fault) leaves Side B fully operational, keeping the server rack powered. The engine computes each side independently in topological order.

### Layer 3 — Dual Rectifier Paths to Server Rack

The `server_rack` node has two DC inputs: `rectifier_a` and `rectifier_b`. The engine checks both every tick:

| rectifier_a | rectifier_b | server_rack state |
|---|---|---|
| alive (48V) | alive (48V) | `NORMAL` |
| alive | dead | `DEGRADED` |
| dead | alive | `DEGRADED` |
| dead | dead | `FAULT` |

The server rack OLED shows `PathA: OK/NO` and `PathB: OK/NO` so you can see the live dual-path status at a glance.

---

## How the Simulation Engine Models This

The engine in `broker/main.py` runs a 1-second tick loop:

1. **Read** — fetch all 25 node states from PostgreSQL `live_status`.
2. **Sort** — topologically sort nodes so parents are always computed before children. The sort respects both `parent_id` and `secondary_parent_id` (the ATS generator path and the server rack's second rectifier).
3. **Propagate** — walk nodes in order, calling `_compute_node()` which applies the redundancy logic for each node type.
4. **Command** — publish a control message to each node's MQTT `/control` topic with the computed state.
5. **Persist** — write updated `v_out`, `status_msg`, `battery_level`, and `gen_timer` back to the DB.

The database is the source of truth for inter-node state. The ESP32 firmware is the source of truth for locally-measured values (CPU load, temperatures, etc.) — the engine defers to whatever the firmware reports unless the topology forces a different state.

---

## Failure Cascade Examples

### Scenario 1 — Utility A Grid Outage (generator starts, Side B unaffected)

```
utility_a → OUTAGE
    → mv_switchgear_a: v_out = 0
    → mv_lv_transformer_a: NO_INPUT
    → ats_a: OPEN (no input)
    → generator_a: STARTING (10-tick delay)
    → lv_dist_a, ups_a, pdu_a, rectifier_a: cascade to OFF/NO_INPUT

After 10 ticks:
    → generator_a: RUNNING (480V)
    → ats_a: GENERATOR (480V restored)
    → lv_dist_a → ups_a → pdu_a → rectifier_a: NORMAL
    → server_rack: DEGRADED → NORMAL (both paths restored)

Side B: unaffected throughout
server_rack during outage: DEGRADED (only rectifier_b alive)
server_rack after recovery: NORMAL
```

### Scenario 2 — Complete Side A Failure (utility + generator both down)

```
utility_a → OUTAGE
generator_a → FAULT

    → ats_a: OPEN
    → all of Side A: NO_INPUT / OFF
    → rectifier_a: OFF (0V DC)

    → server_rack: DEGRADED (rectifier_b still alive)

Side B: fully operational, server rack continues running
```

### Scenario 3 — Both Sides Down

```
utility_a → OUTAGE, generator_a → FAULT
utility_b → OUTAGE, generator_b → FAULT

    → rectifier_a: OFF
    → rectifier_b: OFF
    → server_rack: FAULT (0V, no path)

UPS nodes hold for battery_level ticks before also faulting.
```

---

## Triggering Scenarios Manually

All commands are published with `mosquitto_pub` to the node's `/control` topic. The simulation engine picks up the node's reported state on the next tick and propagates it.

**Prerequisite:** Pi hotspot running, Mosquitto running, at least one node online.

### Simulate a utility outage on Side A

```bash
mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/utility_a/control" \
  -m "STATUS:OUTAGE"
```

Watch `generator_a` on its OLED cycle through `STARTING` → `RUNNING` and `ats_a` flip from `UTILITY` → `GENERATOR`.

### Restore utility on Side A

```bash
mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/utility_a/control" \
  -m "STATUS:GRID_OK"
```

### Force generator fault (no backup available)

```bash
mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/utility_a/control" \
  -m "STATUS:OUTAGE"

mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/generator_a/control" \
  -m "STATUS:FAULT"
```

ATS stays `OPEN`, `server_rack` drops to `DEGRADED` (Side B still up).

### Force both sides down (full 2N failure)

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:OUTAGE"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:FAULT"
```

`server_rack` transitions to `FAULT`.

### Drain a UPS battery

```bash
mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/ups_a/control" \
  -m "BATT:5 STATUS:ON_BATTERY"
```

The engine decrements `battery_level` each tick. When it hits 0 the UPS faults and the downstream PDU, rectifier, and server rack path go dark.

### Manually set server rack path status

```bash
mosquitto_pub -h 192.168.4.1 \
  -t "winter-river/server_rack/control" \
  -m "PATH_A:0 PATH_B:1 STATUS:DEGRADED"
```

---

## Monitoring Live State

### Watch all topic traffic

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
```

### Check retained state for the redundancy-critical nodes

```bash
for node in utility_a generator_a ats_a rectifier_a \
            utility_b generator_b ats_b rectifier_b \
            server_rack; do
  echo -n "$node: "
  mosquitto_sub -h 192.168.4.1 \
    -t "winter-river/$node/status" \
    -C 1 --retained-only 2>/dev/null | python3 -c \
    "import sys,json; d=json.load(sys.stdin); print(d.get('state', d.get('status','?')))"
done
```

### Full system health report

```bash
./scripts/status.sh
```

---

## Node States Quick Reference

| Node | Key states | Redundancy role |
|---|---|---|
| `utility_a/b` | `GRID_OK`, `OUTAGE`, `FAULT` | Root source — outage triggers generator |
| `generator_a/b` | `STANDBY`, `STARTING`, `RUNNING`, `FAULT` | Backup source — 10-tick startup delay |
| `ats_a/b` | `UTILITY`, `GENERATOR`, `OPEN`, `FAULT` | Selects active source for its side |
| `rectifier_a/b` | `NORMAL`, `OFF`, `FAULT` | Final 48V DC feed to server rack |
| `server_rack` | `NORMAL`, `DEGRADED`, `FAULT` | Both paths alive / one alive / none alive |

---

## Database — How State Is Stored

The topology and dual-parent links live in the `nodes` table:

```sql
-- server_rack is fed by both rectifiers
parent_id            = 'rectifier_a'   -- Side A (primary)
secondary_parent_id  = 'rectifier_b'   -- Side B (secondary)

-- ATS is fed by transformer (primary) and generator (backup)
parent_id            = 'mv_lv_transformer_a'
secondary_parent_id  = 'generator_a'
```

Live computed state (updated every tick) is in `live_status`:

```sql
v_out         -- computed output voltage (0.0 = dead)
status_msg    -- current state string (NORMAL, DEGRADED, etc.)
battery_level -- UPS nodes: 0–100
gen_timer     -- generator startup countdown (ticks remaining)
```

If you reset the database with `init_db.sql`, all nodes start with `is_present = FALSE` and `status_msg = 'OFFLINE'` until firmware connects and publishes.
