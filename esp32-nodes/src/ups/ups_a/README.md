# UPS A

ESP32 node simulating Uninterruptible Power Supply A in the Winter River data center training simulator. Sits between the transformer and switchgear in the power topology.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes telemetry JSON to `winter-river/ups_a/status` every 5 seconds
- Subscribes to `winter-river/ups_a/control` for scenario commands
- LWT offline detection
- NTP timestamps from Pi
- 16x2 LCD showing battery %, charge state, load %

## Simulated Metrics

| Metric | Default | Description |
|--------|---------|-------------|
| `battery_pct` | 100 | Battery charge level % |
| `load_pct` | 40 | Output load % |
| `input_v` | 480.0 | AC input voltage |
| `output_v` | 480.0 | AC output voltage |
| `state` | NORMAL | NORMAL / ON_BATTERY / CHARGING / FAULT |

## Auto-Thresholds

| Condition | State |
|-----------|-------|
| battery < 10% or input_v < 400V | `FAULT` |
| battery < 25% or input_v < 440V | `ON_BATTERY` |

## MQTT Control Commands

Send to `winter-river/ups_a/control`:

```
BATT:20        # set battery to 20%
LOAD:75        # set output load to 75%
INPUT:420.0    # set AC input voltage
STATUS:FAULT   # force state string
```

## Telemetry Payload

```json
{"ts":"14:32:01","battery_pct":100,"load_pct":40,"input_v":480.0,"output_v":480.0,"state":"NORMAL","voltage":480}
```

## Build & Flash

```bash
cd esp32-nodes
pio run -e ups_a -t upload
pio device monitor
```
