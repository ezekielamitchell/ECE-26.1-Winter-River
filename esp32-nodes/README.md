# ESP32 Nodes — Firmware Reference

PlatformIO firmware for the 26 ESP32 nodes in the ECE 26.1 Winter River simulator. Each node simulates one component in a block-redundant 2N data-centre power chain, publishes JSON telemetry every 5 seconds, subscribes to MQTT control commands, and drives an SSD1306 128×64 OLED display.

> **Slot budget:** the baseplate has 24 USB-C slots; this firmware tree defines 26 active boards (13 Side A + 13 Side B). Resolve the 2-slot overflow by expanding the baseplate, retiring one rack per side, or carrying two boards on a separate breakout. `bms` is broker-synthesized and the OLED-only `bms` env, if flashed, lives off-baseplate.

---

## Network Configuration

All active nodes connect to the Raspberry Pi hotspot through the shared `esp32-nodes/lib/winter_river/` helper. The common network settings live there rather than being duplicated in every `.cpp` file:

| Constant | Value |
|----------|-------|
| `SSID` | `WinterRiver-AP` |
| `PASSWORD` | `winterriver` |
| `MQTT_SERVER` | `192.168.4.1` |

---

## Shared Firmware Helper

Most active node files are intentionally small because common behaviour lives in `lib/winter_river/`. The helper provides:

- OLED initialization and shared display rows (with 0x3C → 0x3D address probe)
- WiFi reset + reconnect behavior
- NTP setup and timestamp formatting
- MQTT reconnect, LWT, and topic helpers
- token-loop parsing for compound control commands

When adding or updating nodes, prefer extending that helper-driven pattern instead of reintroducing per-file WiFi/MQTT boilerplate.

The Pi must be running its hotspot and Mosquitto before nodes can connect:

```bash
sudo ./scripts/setup_hotspot.sh start
sudo systemctl start mosquitto
```

> **2.4 GHz required:** ESP32 is 2.4 GHz-only. The hotspot is forced to `band bg` channel 6. If nodes fail to connect, verify with `nmcli -g 802-11-wireless.band connection show winter-river-hotspot` → must return `bg`.

---

## Node Inventory

### Side A (13 nodes)

| #  | `node_id`              | Component type dir                       | Rated voltage              |
|----|------------------------|------------------------------------------|----------------------------|
| ①  | `utility_a`            | `utility/utility_a/`                     | 230 kV                     |
| ②  | `mv_switchgear_a`      | `mv_switchgear/mv_switchgear_a/`         | 230 kV (main breaker)      |
| ③  | `hv_mv_transformer_a`  | `hv_mv_transformer/hv_mv_transformer_a/` | 34.5 kV out                |
| ④  | `lv_switchgear_a`      | `lv_switchgear/lv_switchgear_a/`         | 34.5 kV                    |
| ⑤  | `mv_lv_transformer_a`  | `mv_lv_transformer/mv_lv_transformer_a/` | 480 V out                  |
| ⑥  | `generator_a`          | `generator/generator_a/`                 | 480 V                      |
| ⑦  | `ats_a`                | `ats/ats_a/`                             | 480 V (LV transfer switch) |
| ⑧  | `ups_a`                | `ups/ups_a/`                             | 480 V AC                   |
| ⑨  | `cooling_a`            | `cooling/cooling_a/`                     | 480 V (fan bank — 55 fans) |
| ⑩-⑬ | `server_rack_a{1..4}` | `server_rack/` (single shared source)    | 48 V DC                    |

### Side B (13 nodes — mirror of Side A)

All `_a` suffixes replaced with `_b`. Component type directories are identical.

### Broker-synthesized

`bms` is published by `broker/main.py` (no firmware-side telemetry). The `[env:bms]` env compiles an OLED-only mirror that subscribes to `winter-river/bms/status` and renders the rolled-up state — useful if you want a physical BMS panel, but not required for the simulator to function.

---

## Power Chain (per side)

```
utility → hv_mv_transformer → mv_switchgear → mv_lv_transformer
       → lv_switchgear → ats → ups → server_rack_{1..4}
generator ─────────────────────────↗
                                ats ↘ cooling   (parallel mech load)
```

Sides are fully independent (block-redundant 2N — no shared rectifier). Side-A failure kills all 4 side-A racks; side-B continues.

---

## Topic Structure

Each node uses two topics:

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
pio run -e utility_a            --target upload
pio run -e mv_switchgear_a      --target upload
pio run -e hv_mv_transformer_a  --target upload
pio run -e lv_switchgear_a      --target upload
pio run -e mv_lv_transformer_a  --target upload
pio run -e generator_a          --target upload
pio run -e ats_a                --target upload
pio run -e ups_a                --target upload
pio run -e cooling_a            --target upload
pio run -e server_rack_a1       --target upload   # all 8 racks compile the same source
pio run -e server_rack_a2       --target upload   # with WR_NODE_ID / WR_RACK_LABEL set per env
# ... a3, a4, b1..b4

# OLED-only BMS mirror (broker-synthesized topic)
pio run -e bms                  --target upload

# Build all envs without flashing
pio run

# Serial monitor (115200 baud)
pio device monitor
```

---

## Creating a New Node

1. **Create the source file.** Copy the closest existing helper-based node:
   ```bash
   cp -r src/ups/ups_a src/ups/ups_c
   ```
2. **Edit `ups_c.cpp`:** change `NODE_ID`, label, rated voltage, and any component-specific state/constants.
3. **Add a PlatformIO environment** in `platformio.ini`:
   ```ini
   [env:ups_c]
   build_src_filter = +<ups/ups_c/>
   ```
4. **Keep the helper pattern intact.** New nodes should use `wr::begin()`, `wr::timestamp()`, `wr::statusTopic()`, and `wr::forEachToken()` rather than open-coding WiFi/NTP/MQTT setup.
5. **Build and upload:**
   ```bash
   pio run -e ups_c --target upload
   ```
6. **Add a PostgreSQL row** in `scripts/init_db.sql` so the simulation engine tracks it.

> **OLED address:** the shared `wr::begin()` helper already probes `0x3C` then `0x3D` during boot. Keep using that path unless you have a hardware-specific reason to override it.

---

## Firmware Conventions

| Rule | Detail |
|------|--------|
| OLED before WiFi | `Wire.begin()` + `display.begin()` must come before `WiFi.begin()` |
| Full WiFi reset | `WIFI_OFF → delay(200) → WIFI_STA → disconnect → setMinSecurity(WPA_PSK) → begin()` |
| Timeout + restart | 20s WiFi timeout → 30s wait → `ESP.restart()` |
| LWT required | Every node must set a retained LWT OFFLINE on connect |
| Control topic | Every node must subscribe to `winter-river/<node_id>/control` and provide a callback for `wr::begin()` |
| Telemetry interval | Use `wr::TELEMETRY_INTERVAL_MS` |
| NTP | Use `wr::timestamp()` from the shared helper |
| OLED driver | `Adafruit SSD1306` only — never `LiquidCrystal_I2C` |

---

## Libraries

Declared in the shared `[env]` block in `platformio.ini` — available to all environments:

| Library | Purpose |
|---------|---------|
| `knolleary/PubSubClient@^2.8` | MQTT client |
| `adafruit/Adafruit SSD1306@^2.5.7` | 128×64 OLED driver |
| `adafruit/Adafruit GFX Library@^1.11.5` | OLED graphics primitives |

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
- [`src/mv_switchgear/`](src/mv_switchgear/) (no per-component README yet — see `CLAUDE.md` §8 for the control schema)
- [`src/hv_mv_transformer/README.md`](src/hv_mv_transformer/README.md)
- [`src/lv_switchgear/README.md`](src/lv_switchgear/README.md)
- [`src/mv_lv_transformer/README.md`](src/mv_lv_transformer/README.md)
- [`src/generator/README.md`](src/generator/README.md)
- [`src/ats/README.md`](src/ats/README.md)
- [`src/ups/README.md`](src/ups/README.md)
- [`src/cooling/README.md`](src/cooling/README.md)
- [`src/server_rack/README.md`](src/server_rack/README.md)
