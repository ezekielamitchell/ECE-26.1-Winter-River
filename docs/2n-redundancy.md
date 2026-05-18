# 2N Redundancy — How It Works

This document explains how the Winter River simulator implements 2N redundancy: what it means, how the two power paths are wired, how failures cascade and recover, and how to trigger test scenarios manually.

---

## What Is 2N Redundancy?

2N means **two independent, full-capacity power paths** feed every load. If one path fails completely, the other carries 100% of the load with no interruption. Real data centers use this to achieve continuous uptime even during a full side failure.

In Winter River, the load is the `server_rack`. The two utility paths converge at a single shared `rectifier`, which accepts dual AC inputs:

- **Side A** — `utility_a → ... → ups_a → rectifier (primary feed)`
- **Side B** — `utility_b → ... → ups_b → rectifier (secondary feed)`
- **Output** — `rectifier → server_rack (48V DC)`

Both AC feeds run simultaneously. The rectifier reports which feeds are live (PATH_A / PATH_B); the server rack mirrors that status on its OLED.

---

## Full Dual-Path Topology

```
SIDE A                                          SIDE B
──────────────────────────────────────────────────────────────────
utility_a (230kV grid)                          utility_b (230kV grid)
    │                                               │
hv_mv_transformer_a (230kV→34.5kV)          hv_mv_transformer_b (230kV→34.5kV)
    │                                               │
mv_switchgear_a (breaker)                   mv_switchgear_b (breaker)
    │                                               │
mv_lv_transformer_a (→480V)             mv_lv_transformer_b (→480V)
    │                      ┌──────────────────┐     │
    └──────────┐            │    generator_a/b │    │
           ats_a ←──────────┘                └─────→ ats_b
               │   (switches to generator               │
               │    if transformer fails)                │
          lv_dist_a (480V bus)                  lv_dist_b (480V bus)
          ├── cooling_a                         ├── cooling_b
          ├── lighting_a                        ├── lighting_b
          └── ups_a                             └── ups_b
                 │                                     │
                 └──────────┬────────────────────────┘
                            │
                       rectifier (shared, 480V AC → 48V DC, 2N inputs)
                            │
                       server_rack
                       (48V DC)
```

Each side is a complete, independent AC chain. The shared `rectifier` is the 2N convergence point — it receives both AC feeds and produces a single 48V DC bus to the `server_rack`. PATH_A / PATH_B health on the rack OLED reflects the upstream `ups_a` / `ups_b` AC feeds.

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

The two AC sides share nothing upstream of the rectifier. A complete failure of Side A (utility outage + generator fault) leaves Side B fully operational, still delivering 480V AC to the shared rectifier and keeping the server rack powered. The engine computes each side independently in topological order.

### Layer 3 — Dual AC Inputs at the Shared Rectifier

The `rectifier` node has two AC inputs: `ups_a` (primary) and `ups_b` (secondary). The engine checks both every tick:

| ups_a feed | ups_b feed | rectifier state | server_rack state |
|---|---|---|---|
| alive (480V) | alive (480V) | `NORMAL`   | `NORMAL`   |
| alive        | dead         | `DEGRADED` | `DEGRADED` |
| dead         | alive        | `DEGRADED` | `DEGRADED` |
| dead         | dead         | `OFF`      | `FAULT`    |

The server rack OLED shows `PathA: OK/NO` and `PathB: OK/NO` so you can see the live dual-feed status at a glance. The broker mirrors the rectifier's `parent_v_a` / `parent_v_b` onto the rack so its control payload still carries `PATH_A` / `PATH_B`.

---

## How the Simulation Engine Models This

The engine in `broker/main.py` runs a 1-second tick loop:

1. **Read** — fetch all 22 node states from PostgreSQL `live_status`.
2. **Sort** — topologically sort nodes so parents are always computed before children. The sort respects both `parent_id` and `secondary_parent_id` (the ATS generator path and the rectifier's second UPS input).
3. **Propagate** — walk nodes in order, calling `_compute_node()` which applies the redundancy logic for each node type.
4. **Command** — publish a control message to each node's MQTT `/control` topic with the computed state.
5. **Persist** — write updated `v_out`, `status_msg`, `battery_level`, and `gen_timer` back to the DB.

The database is the source of truth for inter-node state. The ESP32 firmware is the source of truth for locally-measured values (CPU load, temperatures, etc.) — the engine defers to whatever the firmware reports unless the topology forces a different state.

---

## Failure Cascade Examples

### Scenario 1 — Utility A Grid Outage (generator starts, Side B unaffected)

```
utility_a → OUTAGE
    → hv_mv_transformer_a: NO_INPUT
    → mv_switchgear_a: v_out = 0
    → mv_lv_transformer_a: NO_INPUT
    → ats_a: OPEN (no input)
    → generator_a: STARTING (10-tick delay)
    → lv_dist_a, ups_a: cascade to OFF/NO_INPUT
    → rectifier: DEGRADED (only ups_b feed alive)

After 10 ticks:
    → generator_a: RUNNING (480V)
    → ats_a: GENERATOR (480V restored)
    → lv_dist_a → ups_a: NORMAL
    → rectifier: NORMAL (both feeds restored)
    → server_rack: DEGRADED → NORMAL

Side B: unaffected throughout
server_rack during outage: DEGRADED (rectifier running on ups_b only)
server_rack after recovery: NORMAL
```

### Scenario 2 — Complete Side A Failure (utility + generator both down)

```
utility_a → OUTAGE
generator_a → FAULT

    → ats_a: OPEN
    → all of Side A AC chain: NO_INPUT / OFF
    → ups_a: drains battery, eventually FAULT

    → rectifier: DEGRADED (running on ups_b feed only)
    → server_rack: DEGRADED

Side B: fully operational, server rack continues running
```

### Scenario 3 — Both Sides Down

```
utility_a → OUTAGE, generator_a → FAULT
utility_b → OUTAGE, generator_b → FAULT

    → ups_a, ups_b: hold for battery_level ticks, then FAULT
    → rectifier: DEGRADED while one UPS still has battery,
                 OFF once both UPS feeds die
    → server_rack: FAULT (0V, no feed)
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

The engine decrements `battery_level` each tick. When it hits 0 the UPS faults; the shared rectifier drops to `DEGRADED` (or `OFF` if both UPS feeds are gone), and the server rack follows.

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
for node in utility_a generator_a ats_a ups_a \
            utility_b generator_b ats_b ups_b \
            rectifier server_rack; do
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
| `rectifier` (shared) | `NORMAL`, `DEGRADED`, `OFF`, `FAULT` | 2N convergence point; dual AC inputs from `ups_a` + `ups_b` |
| `server_rack` | `NORMAL`, `DEGRADED`, `FAULT` | Inherits rectifier state; OLED still reports PATH_A / PATH_B |

---

## Database — How State Is Stored

The topology and dual-parent links live in the `nodes` table:

```sql
-- rectifier (shared) is the 2N convergence point
parent_id            = 'ups_a'         -- Side A AC feed (primary)
secondary_parent_id  = 'ups_b'         -- Side B AC feed (secondary)

-- server_rack is single-fed from the shared rectifier
parent_id            = 'rectifier'
secondary_parent_id  = NULL

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
