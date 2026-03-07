# ESP32 Nodes — Firmware Reference

PlatformIO firmware for all 25 ESP32 nodes in the ECE 26.1 Winter River simulator. Each node simulates one component in a 2N-redundant data centre power chain, publishes JSON telemetry every 5 seconds, subscribes to MQTT control commands, and drives an SSD1306 128×64 OLED display.

---

## Network Configuration

All nodes connect to the Raspberry Pi hotspot. These constants are set at the top of each `.cpp` file:

| Constant | Value |
|----------|-------|
| `ssid` | `WinterRiver-AP` |
| `password` | `winterriver` |
| `mqtt_server` | `192.168.4.1` |

The Pi must be running its hotspot and Mosquitto before nodes can connect:

```bash
sudo ./scripts/setup_hotspot.sh start
sudo systemctl start mosquitto
```

> **2.4 GHz required:** ESP32 is 2.4 GHz-only. The hotspot is forced to `band bg` channel 6. If nodes fail to connect, verify with `nmcli -g 802-11-wireless.band connection show winter-river-hotspot` → must return `bg`.

---

## Node Inventory

### Side A (12 nodes)

| # | `node_id` | Component type dir | Rated voltage |
|---|-----------|-------------------|---------------|
| ① | `utility_a` | `utility/utility_a/` | 230 kV |
| ② | `mv_switchgear_a` | `mv_switchgear/mv_switchgear_a/` | 34.5 kV |
| ③ | `mv_lv_transformer_a` | `mv_lv_transformer/mv_lv_transformer_a/` | 480 V out |
| ④ | `generator_a` | `generator/generator_a/` | 480 V |
| ⑤ | `ats_a` | `ats/ats_a/` | 480 V |
| ⑥ | `lv_dist_a` | `lv_dist/lv_dist_a/` | 480 V |
| ⑦ | `ups_a` | `ups/ups_a/` | 480 V AC |
| ⑧ | `pdu_a` | `pdu/pdu_a/` | 480 V AC |
| ⑨ | `rectifier_a` | `rectifier/rectifier_a/` | 48 V DC |
| ⑩ | `cooling_a` | `cooling/cooling_a/` | 480 V |
| ⑪ | `lighting_a` | `lighting/lighting_a/` | 277 V |
| ⑫ | `monitoring_a` | `monitoring/monitoring_a/` | 120 V |

### Side B (12 nodes — mirror of Side A)

All `_a` suffixes replaced with `_b`. Component type directories are identical.

### Shared (1 node)

| `node_id` | Component type dir | Rated voltage | Parents |
|-----------|--------------------|---------------|---------|
| `server_rack` | `server_rack/server_rack/` | 48 V DC | `rectifier_a` + `rectifier_b` |

---

## Topic Structure

Each node uses three topics:

```
winter-river/<node_id>/status    # Node publishes telemetry (JSON, retained, every 5s)
winter-river/<node_id>/control   # Node subscribes — receives commands from engine
```

The LWT message is also published to `winter-river/<node_id>/status` (retained OFFLINE) so any subscriber immediately sees disconnected nodes.

---

## MQTT LWT / Online Pattern

Every node registers a retained LWT at connect time, then immediately publishes a retained ONLINE message:

```cpp
// LWT — broker publishes this if node drops
String lwt_msg = "{\"node\":\"utility_a\",\"status\":\"OFFLINE\"}";
mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_msg.c_str());

// ONLINE override — published immediately after connect
String online_msg = "{\"ts\":\"14:32:01\",\"node\":\"utility_a\",\"status\":\"ONLINE\"}";
mqtt.publish(status_topic.c_str(), online_msg.c_str(), true);
```

---

## Build & Flash Commands

All commands run from the `esp32-nodes/` directory:

```bash
# Build + flash a single node
pio run -e utility_a --target upload
pio run -e mv_switchgear_a --target upload
pio run -e mv_lv_transformer_a --target upload
pio run -e generator_a --target upload
pio run -e ats_a --target upload
pio run -e lv_dist_a --target upload
pio run -e ups_a --target upload
pio run -e pdu_a --target upload
pio run -e rectifier_a --target upload
pio run -e cooling_a --target upload
pio run -e lighting_a --target upload
pio run -e monitoring_a --target upload
pio run -e server_rack --target upload

# Flash Side B (same pattern with _b suffix)
pio run -e utility_b --target upload
# ... etc.

# Build all 25 envs without flashing
pio run

# Serial monitor (115200 baud)
pio device monitor
```

---

## Creating a New Node

1. **Create the source file.** Copy the closest existing node:
   ```bash
   cp -r src/ups/ups_a src/ups/ups_c
   ```
2. **Edit `ups_c.cpp`:** change `node_id`, rated voltage, and any component-specific constants.
3. **Add a PlatformIO environment** in `platformio.ini`:
   ```ini
   [env:ups_c]
   build_src_filter = +<ups/ups_c/>
   ```
4. **Build and upload:**
   ```bash
   pio run -e ups_c --target upload
   ```
5. **Add a PostgreSQL row** in `scripts/init_db.sql` so the simulation engine tracks it.

> **OLED address:** new nodes should hardcode `0x3C`. Only use `detectOLEDAddr()` if you physically confirm the SA0 pin is pulled HIGH (making the module `0x3D`).

---

## Firmware Conventions

| Rule | Detail |
|------|--------|
| OLED before WiFi | `Wire.begin()` + `display.begin()` must come before `WiFi.begin()` |
| Full WiFi reset | `WIFI_OFF → delay(200) → WIFI_STA → disconnect → setMinSecurity(WPA_PSK) → begin()` |
| Timeout + restart | 20s WiFi timeout → 30s wait → `ESP.restart()` |
| LWT required | Every node must set a retained LWT OFFLINE on connect |
| Control topic | Every node must subscribe to `winter-river/<node_id>/control` and implement `mqttCallback` |
| Telemetry interval | `delay(5000)` at end of `loop()` |
| NTP | `configTime(-28800, 3600, "192.168.4.1")` — Pacific time, served by Pi |
| OLED driver | `Adafruit SSD1306` only — never `LiquidCrystal_I2C` |

---

## Libraries

Declared in the shared `[env]` block in `platformio.ini` — available to all environments:

| Library | Purpose |
|---------|---------|
| `knolleary/PubSubClient@^2.8` | MQTT client |
| `adafruit/Adafruit SSD1306@^2.5.7` | 128×64 OLED driver |
| `adafruit/Adafruit GFX Library@^1.11.5` | OLED graphics primitives |

> `LiquidCrystal_I2C` is **not** included. The only legacy node that used it (`pdu_b`) has been replaced with SSD1306-based firmware.

---

## Testing

```bash
# Watch all node telemetry live
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v

# Watch a single node
mosquitto_sub -h 192.168.4.1 -t "winter-river/utility_a/status" -v

# Send a control command
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE"

# Full system health check (run on Pi)
./scripts/status.sh
```

For per-node control commands, see the `README.md` inside each component type directory:

- [`src/utility/README.md`](src/utility/README.md)
- [`src/mv_switchgear/README.md`](src/mv_switchgear/README.md)
- [`src/mv_lv_transformer/README.md`](src/mv_lv_transformer/README.md)
- [`src/generator/README.md`](src/generator/README.md)
- [`src/ats/README.md`](src/ats/README.md)
- [`src/lv_dist/README.md`](src/lv_dist/README.md)
- [`src/ups/README.md`](src/ups/README.md)
- [`src/pdu/README.md`](src/pdu/README.md)
- [`src/rectifier/README.md`](src/rectifier/README.md)
- [`src/cooling/README.md`](src/cooling/README.md)
- [`src/lighting/README.md`](src/lighting/README.md)
- [`src/monitoring/README.md`](src/monitoring/README.md)
- [`src/server_rack/README.md`](src/server_rack/README.md)
