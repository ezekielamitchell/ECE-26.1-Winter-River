# Switchgear A

ESP32 node simulating Switchgear / Main Breaker Panel A in the Winter River data center training simulator. Sits between the transformer and UPS, controlling and monitoring the main breaker state.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes telemetry JSON to `winter-river/sw_a/status` every 5 seconds
- Subscribes to `winter-river/sw_a/control` for scenario commands
- LWT offline detection
- NTP timestamps from Pi
- 16x2 LCD showing breaker state, current draw, load %

## Simulated Metrics

| Metric | Default | Description |
|--------|---------|-------------|
| `breaker` | true | Main breaker closed (true) / open (false) |
| `current_a` | 120.5 | Current draw in amps |
| `load_kw` | 86.5 | Active power in kW |
| `load_pct` | 35 | % of rated capacity |
| `state` | CLOSED | CLOSED / OPEN / TRIPPED / FAULT |

## Auto-Thresholds

| Condition | State |
|-----------|-------|
| current > 280A or load > 95% | `TRIPPED` (breaker opens) |
| current > 220A or load > 80% | `FAULT` |

## MQTT Control Commands

Send to `winter-river/sw_a/control`:

```
OPEN           # open the main breaker
CLOSE          # close the main breaker
LOAD:70        # set load % (also updates current_a and load_kw)
STATUS:FAULT   # force state string
```

## Telemetry Payload

```json
{"ts":"14:32:01","breaker":true,"current_a":120.5,"load_kw":86.5,"load_pct":35,"state":"CLOSED","voltage":480}
```

## Build & Flash

```bash
cd esp32-nodes
pio run -e sw_a -t upload
pio device monitor
```
