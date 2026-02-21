# Transformer A

ESP32 node simulating a 480V / 500kVA transformer in the Winter River data center training simulator.

## Features

- Connects to Pi hotspot (`WinterRiver-AP`) and Mosquitto broker at `192.168.4.1`
- Publishes status JSON to `winter-river/trf_a/status` every 5 seconds
- Subscribes to `winter-river/trf_a/control` for real-time scenario commands
- Last Will and Testament (LWT): broker auto-publishes `OFFLINE` to the status topic if the node disconnects unexpectedly
- NTP time sync from the Pi (`192.168.4.1`) — retries up to 10 times, continues without time if sync fails
- 128x64 OLED display (I2C, address `0x3C`) showing metrics and connection status
- Simulated metrics: load %, power kVA, winding temperature, fault status
- Auto-threshold warnings (>75% load or >149°F) and faults (>90% load or >185°F)

## Network

| Setting | Value |
|---------|-------|
| WiFi SSID | `WinterRiver-AP` |
| WiFi Password | `winterriver` |
| MQTT Broker | `192.168.4.1:1883` |
| NTP Server | `192.168.4.1` (Pi, served to `192.168.4.0/24`) |
| Timezone offset | PST (UTC−8), `gmt_offset_sec = -28800` |

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `winter-river/trf_a/status` | Publish | JSON telemetry every 5 s; also used for LWT (`OFFLINE`) and connect (`ONLINE`) |
| `winter-river/trf_a/control` | Subscribe | Plain-text commands (see below) |

## MQTT Control Commands

Send to `winter-river/trf_a/control`:

```
LOAD:80       # set load to 80%
TEMP:160      # set winding temp to 160°F
STATUS:FAULT  # force status string
```

Auto-thresholds override the status string after any command:

| Condition | Status |
|-----------|--------|
| load > 90% **or** temp > 185°F | `FAULT` |
| load > 75% **or** temp > 149°F | `WARNING` |
| otherwise | `NORMAL` (or last `STATUS:` value) |

## Telemetry Payload

Published to `winter-river/trf_a/status` every 5 seconds:

```json
{
  "ts": "14:32:01",
  "load": 45,
  "power_kva": 225.0,
  "temp_f": 108,
  "status": "NORMAL",
  "voltage": 480
}
```

## Build & Flash

**All PlatformIO commands must be run from the `/esp32-nodes/` folder.**

```bash
cd esp32-nodes
pio run -e transformer_a           # compile
pio run -e transformer_a -t upload # flash
pio device monitor                 # serial output (115200 baud)
```
