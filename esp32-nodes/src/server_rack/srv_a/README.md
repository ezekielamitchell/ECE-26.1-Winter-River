# Server Rack A

ESP32 node simulating Server Rack A in the Winter River data center training simulator. Sits at the bottom of the power chain, downstream of PDU A.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes telemetry JSON to `winter-river/srv_a/status` every 5 seconds
- Subscribes to `winter-river/srv_a/control` for scenario commands
- LWT offline detection
- NTP timestamps from Pi
- 16x2 LCD showing CPU load %, thermal state, inlet temp

## Simulated Metrics

| Metric | Default | Description |
|--------|---------|-------------|
| `cpu_pct` | 42 | Aggregate CPU utilization % |
| `inlet_f` | 75 | Rack inlet air temperature (°F) |
| `power_kw` | 3.2 | Total rack power draw (kW) — scales with CPU load |
| `units` | 8 | Number of active server units in rack |
| `state` | NORMAL | NORMAL / THROTTLED / FAULT |

## Auto-Thresholds

| Condition | State |
|-----------|-------|
| inlet > 95°F or cpu > 95% | `FAULT` |
| inlet > 85°F or cpu > 80% | `THROTTLED` |
| otherwise | `NORMAL` |

## MQTT Control Commands

Send to `winter-river/srv_a/control`:

```
CPU:80         # set CPU load % (also updates power_kw)
TEMP:90        # set inlet temperature (°F)
UNITS:4        # set number of active server units
STATUS:FAULT   # force state string
```

## Telemetry Payload

```json
{"ts":"14:32:01","cpu_pct":42,"inlet_f":75,"power_kw":3.2,"units":8,"state":"NORMAL","voltage":208}
```

## Build & Flash

```bash
cd esp32-nodes
pio run -e srv_a -t upload
pio device monitor
```
