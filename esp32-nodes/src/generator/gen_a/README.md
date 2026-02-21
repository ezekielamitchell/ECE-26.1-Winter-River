# Generator A

ESP32 node simulating Generator A in the Winter River data center training simulator. Sits upstream of the transformer — activated during utility power loss scenarios.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes telemetry JSON to `winter-river/gen_a/status` every 5 seconds
- Subscribes to `winter-river/gen_a/control` for scenario commands
- LWT offline detection
- NTP timestamps from Pi
- 16x2 LCD showing fuel %, run state, RPM

## Simulated Metrics

| Metric | Default | Description |
|--------|---------|-------------|
| `fuel_pct` | 85 | Fuel tank level % |
| `rpm` | 0 | Engine RPM (0 = off/standby) |
| `output_v` | 0.0 | Generator output voltage (0 when stopped) |
| `load_pct` | 0 | Output load % |
| `state` | STANDBY | STANDBY / STARTING / RUNNING / FAULT |

## Auto-Thresholds

| Condition | State |
|-----------|-------|
| fuel < 5% or running with rpm < 800 | `FAULT` |
| rpm > 1500 | `RUNNING` (output_v = 480V) |
| 0 < rpm ≤ 1500 | `STARTING` |
| rpm = 0 | `STANDBY` (output_v = 0V) |

## MQTT Control Commands

Send to `winter-river/gen_a/control`:

```
FUEL:50        # set fuel level to 50%
RPM:1800       # set engine RPM (>1500 = RUNNING)
LOAD:60        # set output load %
STATUS:FAULT   # force state string
```

## Telemetry Payload

```json
{"ts":"14:32:01","fuel_pct":85,"rpm":0,"output_v":0.0,"load_pct":0,"state":"STANDBY","voltage":480}
```

## Build & Flash

```bash
cd esp32-nodes
pio run -e gen_a -t upload
pio device monitor
```
