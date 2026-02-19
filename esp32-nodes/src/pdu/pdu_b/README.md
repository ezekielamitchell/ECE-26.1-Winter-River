# PDU B

ESP32 node simulating Power Distribution Unit B in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes heartbeat to `winter-river` topic every second
- ADC voltage reading on pin 34 (averaged over 64 samples, 0–3.3V range)
- 16x2 LCD display (I2C, address `0x3F`) showing node ID and local IP
- 480V voltage rating

## Network

| Setting | Value |
|---------|-------|
| WiFi SSID | `WinterRiver-AP` |
| WiFi Password | `winterriver` |
| MQTT Broker | `192.168.4.1:1883` |

## Build & Flash

```bash
cd esp32-nodes
pio run -e pdu_b
pio run -e pdu_b -t upload
pio device monitor
```
