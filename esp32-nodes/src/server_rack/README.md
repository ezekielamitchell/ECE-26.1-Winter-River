# Server Rack — `server_rack_{a1..a3,b1..b3}`

## Real-World Role

Server racks are the IT load — the reason the entire power and cooling infrastructure exists. Winter River models 6 racks total (3 per side). Each rack is **single-fed from its side's UPS** (no shared rectifier, no rack-level 2N): a side failure kills all 3 of that side's racks at once. Redundancy lives at the side (block) level, not per-rack. Hot-aisle temperature is driven by the broker thermal model (`broker/thermal.py`) and pushed to each rack's `TEMP:<f>` control every tick.

---

## Nodes in This Topology

| node_id          | Side | Rated Voltage | Parent   | Notes                                |
|------------------|------|---------------|----------|--------------------------------------|
| `server_rack_a1` | A    | 48 V DC       | `ups_a`  | One of three side-A racks            |
| `server_rack_a2` | A    | 48 V DC       | `ups_a`  |                                      |
| `server_rack_a3` | A    | 48 V DC       | `ups_a`  |                                      |
| `server_rack_b1` | B    | 48 V DC       | `ups_b`  | One of three side-B racks            |
| `server_rack_b2` | B    | 48 V DC       | `ups_b`  |                                      |
| `server_rack_b3` | B    | 48 V DC       | `ups_b`  |                                      |

All 6 envs compile **the same source file** (`src/server_rack/server_rack.cpp`). PlatformIO `build_flags` inject `WR_NODE_ID` and `WR_RACK_LABEL` per env, so the firmware identifies itself and labels the OLED correctly.

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field      | Type   | Default  | Description                                |
|------------|--------|----------|--------------------------------------------|
| `ts`       | string | HH:MM:SS | Timestamp from NTP                         |
| `cpu_pct`  | int    | 42       | CPU utilisation (%)                        |
| `inlet_f`  | int    | 75       | Rack inlet temperature (°F)                |
| `power_kw` | float  | 3.2      | Total rack power draw (kW)                 |
| `units`    | int    | 8        | Number of active server units              |
| `state`    | string | NORMAL   | Rack health state                          |
| `voltage`  | int    | 48       | Rated DC voltage (V)                       |

There is no `path_a` / `path_b` field — the shared rectifier (and its dual feeds) was removed when the topology shifted to block-redundant 2N.

---

## States

| State      | Meaning                                                              |
|------------|----------------------------------------------------------------------|
| `NORMAL`   | UPS on grid, inlet temp and CPU within limits                       |
| `DEGRADED` | UPS `ON_BATTERY`, OR inlet > 85 °F, OR CPU > 80 %                   |
| `FAULT`    | UPS dead, OR inlet > 95 °F, OR CPU > 95 %                           |

The UPS-driven state is set by `broker/main.py`: the rack is `DEGRADED` only while its parent UPS is `ON_BATTERY`, and `FAULT` when the UPS is dead — it returns to `NORMAL` once the side is back on utility/generator power, even while the UPS is still `CHARGING` its battery. Thermal / CPU thresholds are evaluated locally in the firmware.

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                |
|------------------|-------------------|-------------------------------------------------------|
| `CPU:<pct>`      | `CPU:90`          | Set CPU utilisation; auto-calculates `power_kw`       |
| `TEMP:<f>`       | `TEMP:92`         | Set rack inlet temperature (°F)                       |
| `UNITS:<n>`      | `UNITS:12`        | Set number of active server units                     |
| `INPUT:<v>`      | `INPUT:480`       | Broker sends 480 when fed, 0 when UPS is dead         |
| `STATUS:<state>` | `STATUS:DEGRADED` | Force state string (used for manual fault injection)  |

`TEMP` is normally driven by `broker/thermal.py`'s hot-aisle output — see `winter-river/facility/status` for the model state.

---

## Build & Flash

All 6 racks build the same source with per-env build_flags:

```bash
# Flash one rack
pio run -e server_rack_a1 --target upload
pio run -e server_rack_b3 --target upload

# Build all six
for env in server_rack_a1 server_rack_a2 server_rack_a3 \
           server_rack_b1 server_rack_b2 server_rack_b3; do
    pio run -e "$env"
done
```

The `[env:server_rack_*]` entries in `platformio.ini` all point at `build_src_filter = +<server_rack/>` and differ only in their `build_flags`.
