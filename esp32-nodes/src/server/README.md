# Server

ESP32 node simulating a server rack load in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes heartbeat string to `winter-river` topic every second
- 16x2 LCD display (I2C) showing node ID and local IP
- 480V voltage rating
- Downstream of PDU nodes in the power topology

## Network

| Setting | Value |
|---------|-------|
| WiFi SSID | `WinterRiver-AP` |
| WiFi Password | `winterriver` |
| MQTT Broker | `192.168.4.1:1883` |

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `winter-river` | Publish | Heartbeat string every 1 s |

## Display

16x2 LCD via I2C (`LiquidCrystal_I2C`). Default address is `0x3F` — if the display shows only a backlight with no text, try `0x27` instead (edit the constructor in `server.cpp` line 21).

Library provided by `marcoschwartz/LiquidCrystal_I2C@^1.1.4` — declared in the shared `[env]` block of `platformio.ini`.

## Build & Flash

```bash
cd esp32-nodes
pio run -e server           # compile
pio run -e server -t upload # flash
pio device monitor          # serial output (115200 baud)
```
