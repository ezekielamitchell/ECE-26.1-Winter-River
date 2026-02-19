# PDU A

ESP32 node simulating Power Distribution Unit A in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes heartbeat to `winter-river` topic every second
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
pio run -e pdu_a
pio run -e pdu_a -t upload
pio device monitor
```
