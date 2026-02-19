# Transformer A

ESP32 node simulating a 480V / 500kVA transformer in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes status JSON to `winter-river/trf_a/status` every 5 seconds
- Subscribes to `winter-river/trf_a/control` for real-time scenario commands
- Last Will and Testament (LWT) for automatic offline detection
- NTP time sync (PST, `pool.ntp.org`) for JSON timestamps
- 128x64 OLED display (I2C, address `0x3C`) showing metrics and connection status
- Simulated metrics: load %, power kVA, winding temperature, fault status
- Auto-threshold warnings (>75% load or >149°F) and faults (>90% load or >185°F)

## Network

| Setting | Value |
|---------|-------|
| WiFi SSID | `WinterRiver-AP` |
| WiFi Password | `winterriver` |
| MQTT Broker | `192.168.4.1:1883` |

## MQTT Control Commands

Send to `winter-river/trf_a/control`:

```
LOAD:80       # set load to 80%
TEMP:160      # set winding temp to 160°F
STATUS:FAULT  # force status string
```

## Build & Flash

**All PlatformIO commands must be run from the `/esp32-nodes/` folder.**

```bash
cd esp32-nodes
pio run -e transformer_a
pio run -e transformer_a -t upload
pio device monitor
```
