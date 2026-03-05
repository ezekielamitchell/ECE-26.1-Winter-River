# Server Rack — `server_rack`

## Real-World Role

The server rack is the IT load endpoint — the reason the entire power and cooling infrastructure exists. In modern hyperscale data centers, racks are fed with redundant 48V DC power from dual rectifier paths (2N architecture), so either path can carry full load independently. Each server has dual power supplies (PSU A and PSU B) connected to opposite power paths, meaning no single component failure anywhere in the Side A or Side B chain should cause a rack outage. A DEGRADED state indicates one path has been lost and the rack is now vulnerable to any second failure — an event requiring immediate operator response to restore redundancy before any further action is taken.

---

## Nodes in This Topology

| node_id       | Side   | Rated Voltage | Primary Parent | Secondary Parent |
|---------------|--------|---------------|----------------|------------------|
| `server_rack` | A + B  | 48 V DC       | `rectifier_a`  | `rectifier_b`    |

This is a 2N shared node. Both rectifier paths feed it simultaneously and independently.

---

## Telemetry (published every 5s)

Topic: `winter-river/server_rack/status`

| Field      | Type   | Default  | Description                                  |
|------------|--------|----------|----------------------------------------------|
| `ts`       | string | HH:MM:SS | Timestamp from NTP                           |
| `cpu_pct`  | int    | 42       | Aggregate CPU utilisation (%)                |
| `inlet_f`  | int    | 75       | Rack inlet temperature (°F)                  |
| `power_kw` | float  | 3.2      | Total rack power draw (kW)                   |
| `units`    | int    | 8        | Number of active server units                |
| `path_a`   | int    | 1        | Side A rectifier path live (1 = yes, 0 = no) |
| `path_b`   | int    | 1        | Side B rectifier path live (1 = yes, 0 = no) |
| `state`    | string | NORMAL   | Rack health state                            |
| `voltage`  | int    | 48       | Rated DC voltage (V)                         |

---

## States

| State      | Meaning                                                              |
|------------|----------------------------------------------------------------------|
| `NORMAL`   | Both power paths live — full 2N redundancy active                   |
| `DEGRADED` | One path lost — running on single feed, no redundancy margin        |
| `FAULT`    | Both paths dead, thermal limit exceeded, or CPU critically overloaded|

---

## MQTT Control Commands

Topic: `winter-river/server_rack/control`

| Command          | Example           | Effect                                                               |
|------------------|-------------------|----------------------------------------------------------------------|
| `CPU:<pct>`      | `CPU:90`          | Set CPU utilisation; auto-calculates `power_kw`                      |
| `TEMP:<f>`       | `TEMP:92`         | Set rack inlet temperature (°F)                                      |
| `UNITS:<n>`      | `UNITS:12`        | Set number of active server units                                    |
| `PATH_A:<0\|1>`  | `PATH_A:0`        | Set Side A rectifier path status (1 = live, 0 = dead)               |
| `PATH_B:<0\|1>`  | `PATH_B:0`        | Set Side B rectifier path status (1 = live, 0 = dead)               |
| `STATUS:<state>` | `STATUS:DEGRADED` | Force state string                                                   |

`PATH_A` and `PATH_B` are updated automatically by the simulation engine (`broker/main.py`) based on computed `v_out` of `rectifier_a` and `rectifier_b`. Manual PATH commands are for fault injection and testing only.

---

## Auto-Thresholds

| Condition                                  | Resulting State |
|--------------------------------------------|-----------------|
| `path_a == 0 && path_b == 0`               | `FAULT`         |
| `inlet_f > 95` or `cpu_pct > 95`           | `FAULT`         |
| `path_a == 0` or `path_b == 0`             | `DEGRADED`      |
| `inlet_f > 85` or `cpu_pct > 80`           | `DEGRADED`      |
| Both paths live, temp and CPU within limits | `NORMAL`        |

---

## Build & Flash

```bash
# server_rack is a single shared node (no _a / _b suffix)
pio run -e server_rack --target upload

# Build only (no flash)
pio run -e server_rack
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/server_rack/status" -v

# Simulate Side A path failure (rack enters DEGRADED)
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack/control" -m "PATH_A:0"

# Simulate high CPU load and elevated inlet temperature
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack/control" -m "CPU:88 TEMP:87"

# Restore both paths (return to NORMAL)
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack/control" -m "PATH_A:1 PATH_B:1"
