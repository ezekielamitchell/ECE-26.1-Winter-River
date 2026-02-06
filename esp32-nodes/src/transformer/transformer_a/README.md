# Transformer A

ESP32 node simulating a 480V transformer in the Winter River data center training simulation.

## Features

- Publishes status JSON to MQTT broker (load, temp, voltage, status)
- OLED display showing node metrics and connection status
- Receives MQTT commands to simulate scenarios (LOAD, TEMP, STATUS)
- Last Will and Testament for offline detection
- NTP time sync for timestamps

## Build

**All PlatformIO commands must be run from the `/esp32-nodes/` folder.**

```bash
cd esp32-nodes
pio run -e transformer_a
pio run -e transformer_a -t upload
```
