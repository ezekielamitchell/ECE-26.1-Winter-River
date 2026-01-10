# ESP32 Firmware

This directory contains the PlatformIO-based firmware for ESP32 DevKitC v1 nodes.

## Hardware

- **Board**: ESP32 DevKitC v1
- **Framework**: Arduino
- **Platform**: ESP32 (espressif32)

## Setup

1. Install PlatformIO Core or use PlatformIO IDE extension for VSCode
2. Copy `include/config.sample.h` to `include/config.h`
3. Edit `include/config.h` with your WiFi credentials and MQTT broker settings
4. Never commit `include/config.h` (it's git-ignored)

## Configuration

All sensitive configuration (WiFi SSID, password, MQTT credentials) should be in `include/config.h`.

## Building

```bash
# From this directory (esp/)
pio run

# Build and upload
pio run --target upload

# Monitor serial output
pio device monitor
```

## Development Tasks

- [ ] Implement WiFi connection logic in `src/main.cpp`
- [ ] Implement MQTT client connection
- [ ] Add sensor reading functionality
- [ ] Add command handling (subscriptions)
- [ ] Add telemetry publishing
- [ ] Implement reconnection logic
- [ ] Add OTA update support (optional)

## Libraries Used

- **PubSubClient**: MQTT client library
- **ArduinoJson**: JSON parsing and generation

Add additional libraries in `platformio.ini` as needed for your sensors and actuators.
