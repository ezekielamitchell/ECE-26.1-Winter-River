# PDU B

ESP32 node simulating Power Distribution Unit B in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes heartbeat string to `winter-river` topic every second
- ADC voltage reading on GPIO 34 — averaged over 64 samples, 0–3.3V range (11dB attenuation)
- 16x2 LCD display (I2C) showing node ID and local IP
- 480V voltage rating

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

## ADC Note

GPIO 34 is sampled 64 times per loop and averaged to reduce ESP32 ADC noise. The raw value maps to 0–3.3V via:

```cpp
double voltage = esp_raw_value * (3.3 / 4095.0);
```

ADC1 pins (GPIO 32–39) must be used when WiFi is active — ADC2 is internally shared with the WiFi radio.

## Display

16x2 LCD via I2C (`LiquidCrystal_I2C`). Default address is `0x3F` — if the display shows only a backlight with no text, try `0x27` instead (edit the constructor in `pdu_b.cpp` line 25).

Library provided by `marcoschwartz/LiquidCrystal_I2C@^1.1.4` — declared in the shared `[env]` block of `platformio.ini`.

## Build & Flash

```bash
cd esp32-nodes
pio run -e pdu_b           # compile
pio run -e pdu_b -t upload # flash
pio device monitor         # serial output (115200 baud)
```
