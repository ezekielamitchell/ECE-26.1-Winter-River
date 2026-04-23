# Communication Protocols & Messaging — ECE 26.1 Winter River

> **Source of truth:** This document reflects the actual implemented code in
> `esp32-nodes/src/`, `broker/main.py`, `grafana/telegraf.conf`, and
> `deploy/mosquitto_setup.sh` as of Winter Quarter 2026.

---

## 1. Transport Layer

All inter-component communication uses **MQTT** (Message Queuing Telemetry Transport).

| Property | Value |
|---|---|
| Broker | Mosquitto (system package on Raspberry Pi) |
| Broker host | `192.168.4.1` (Pi hotspot gateway) |
| TCP port | `1883` (ESP32 nodes, Telegraf, `broker/main.py`) |
| WebSocket port | `9001` (Grafana MQTT Live datasource plugin) |
| Authentication | Anonymous — `allow_anonymous true` |
| Binding | `0.0.0.0` (all interfaces) |
| Persistence | Enabled — retained messages survive broker restarts |
| Keepalive | 60 seconds (configured in `broker/config.sample.toml`) |
| Logging | `/var/log/mosquitto/mosquitto.log` |

> **Config ownership:** `/etc/mosquitto/mosquitto.conf` is fully managed by
> `deploy/mosquitto_setup.sh`. Never edit it manually — re-run the script to update.

---

## 2. Topic Structure

```
winter-river/
├── <node_id>/status      # Telemetry: ESP32 → broker (retained)
└── <node_id>/control     # Commands:  broker → ESP32
```

### Active Node IDs (25 total)

| Side | Nodes (in power-chain order) |
|---|---|
| Side A | `utility_a`, `mv_switchgear_a`, `mv_lv_transformer_a`, `generator_a`, `ats_a`, `lv_dist_a`, `ups_a`, `pdu_a`, `rectifier_a`, `cooling_a`, `lighting_a`, `monitoring_a` |
| Side B | `utility_b`, `mv_switchgear_b`, `mv_lv_transformer_b`, `generator_b`, `ats_b`, `lv_dist_b`, `ups_b`, `pdu_b`, `rectifier_b`, `cooling_b`, `lighting_b`, `monitoring_b` |
| Shared | `server_rack` |

---

## 3. QoS Levels (from source)

QoS levels are **not uniform** — they are set per direction and per client:

| Direction | Client | QoS | Source |
|---|---|---|---|
| ESP32 LWT registration | ESP32 (`PubSubClient`) | **QoS 1** | `mqtt.connect(..., 1, true, lwt_msg)` |
| ESP32 telemetry publish | ESP32 (`PubSubClient`) | **QoS 0** | `mqtt.publish(topic, payload, true)` *(retained, no QoS param)* |
| ESP32 ONLINE override publish | ESP32 (`PubSubClient`) | **QoS 0** | `mqtt.publish(lwt_topic, online, true)` *(retained)* |
| ESP32 control subscribe | ESP32 (`PubSubClient`) | **QoS 0** | `mqtt.subscribe(ctrl.c_str())` *(no QoS param → default 0)* |
| Broker status subscribe | `broker/main.py` (paho-mqtt) | **QoS 1** | `client.subscribe("winter-river/+/status", qos=1)` |
| Broker control publish | `broker/main.py` (paho-mqtt) | **QoS 1** | `client.publish(..., qos=1)` |
| Telegraf status subscribe | Telegraf MQTT consumer | **QoS 0** | `qos = 0` in `telegraf.conf` |

**Summary:**
- LWT delivery and broker-side messaging: **QoS 1** (at-least-once)
- Node-to-broker telemetry and Telegraf ingestion: **QoS 0** (fire-and-forget), compensated by retained flag and 5s repeat interval
- Missing data from Telegraf is acceptable — Grafana shows the last retained value

---

## 4. Retained Messages

Retained messages allow any subscriber (including `status.sh`, Grafana, new broker instances) to immediately receive the last known state without waiting for the next 5-second publish cycle.

| Topic | Retained | Content |
|---|---|---|
| `winter-river/<node_id>/status` | **Yes** | Telemetry JSON or LWT/ONLINE payload |
| `winter-river/<node_id>/control` | No | Command string (not retained) |

---

## 5. LWT (Last Will and Testament)

Every node registers an LWT at connect time. If a node disconnects unexpectedly (power loss, WiFi drop), the broker automatically publishes the LWT to the node's `/status` topic. This is the primary cascade failure trigger.

### LWT Registration (at connect)

```cpp
// From utility_a.cpp — identical pattern on all nodes
mqtt.connect(
    node_id,               // client ID
    lwt_topic.c_str(),     // "winter-river/<node_id>/status"
    1,                     // QoS 1
    true,                  // retained
    lwt_msg.c_str()        // {"node":"utility_a","status":"OFFLINE"}
)
```

### LWT Payload (broker fires on disconnect)

```json
{"node":"utility_a","status":"OFFLINE"}
```

### ONLINE Override (published immediately after connect)

```json
{"ts":"14:32:01","node":"utility_a","status":"ONLINE"}
```

The ONLINE message is published **retained** on the same `/status` topic, overwriting the LWT before any subscriber sees it — as long as the node connects successfully.

---

## 6. Telemetry Payloads (ESP32 → Broker)

- **Format:** JSON
- **Interval:** Every 5 seconds (`delay(5000)` at end of `loop()`)
- **Retained:** Yes
- **QoS:** 0

All payloads include `"ts"` (HH:MM:SS from NTP) and `"state"`.

```json
// utility_a / utility_b
{"ts":"14:32:01","v_out":230.0,"freq_hz":60.0,"load_pct":12,"state":"GRID_OK","voltage_kv":230.0,"phase":3}

// mv_switchgear_a / _b
{"ts":"14:32:01","breaker":true,"current_a":120.5,"load_kw":86.5,"load_pct":35,"state":"CLOSED","voltage":230000}

// mv_lv_transformer_a / _b
{"ts":"14:32:01","load_pct":45,"power_kva":225.0,"temp_f":108,"state":"NORMAL","voltage":480}

// generator_a / _b
{"ts":"14:32:01","fuel_pct":85,"rpm":0,"output_v":0.0,"load_pct":0,"state":"STANDBY","voltage":480}

// ats_a / _b
{"ts":"14:32:01","source":"UTILITY","input_v":480.0,"output_v":480.0,"load_pct":35,"state":"UTILITY","voltage":480}

// lv_dist_a / _b
{"ts":"14:32:01","input_v":480.0,"ups_load_kw":95.0,"mech_load_kw":42.0,"total_load_kw":137.0,"load_pct":36,"source":"UTILITY","state":"NORMAL","voltage":480.0}

// ups_a / _b
{"ts":"14:32:01","battery_pct":100,"load_pct":40,"input_v":480.0,"output_v":480.0,"state":"NORMAL","voltage":480}

// pdu_a / pdu_b
{"ts":"14:32:01","input_v":480.0,"output_v":480.0,"load_pct":25,"state":"NORMAL","voltage":480}

// rectifier_a / _b
{"ts":"14:32:01","input_v_ac":480.0,"output_v_dc":48.0,"load_pct":30,"state":"NORMAL","ac_voltage":480,"dc_voltage":48}

// cooling_a / _b
{"ts":"14:32:01","input_v":480.0,"coolant_temp_f":65,"fan_speed_pct":60,"load_pct":60,"state":"NORMAL","voltage":480}

// lighting_a / _b
{"ts":"14:32:01","input_v":277.0,"zones_active":4,"brightness_pct":100,"load_pct":40,"state":"NORMAL","voltage":277}

// monitoring_a / _b
{"ts":"14:32:01","input_v":120.0,"sensor_count":12,"alert_count":0,"uptime_pct":100,"load_pct":15,"state":"NORMAL","voltage":120}

// server_rack (2N shared)
{"ts":"14:32:01","cpu_pct":42,"inlet_f":75,"power_kw":3.2,"units":8,"path_a":1,"path_b":1,"state":"NORMAL","voltage":48}
```

---

## 7. Control Commands (Broker → ESP32)

- **Format:** Space-separated `KEY:value` tokens — **not JSON**
- **Retained:** No
- **QoS:** 1

The `mqttCallback` on each ESP32 uses a token-loop parser. Multiple tokens in a single message are all processed (compound commands).

```
# Examples of compound commands from broker/main.py
VOLT:230000.0 STATUS:GRID_OK
CLOSE STATUS:CLOSED
RPM:1800 STATUS:RUNNING
SOURCE:GENERATOR STATUS:GENERATOR
INPUT:480.0 BATT:100 STATUS:NORMAL
INPUT_AC:480.0 STATUS:NORMAL
PATH_A:1 PATH_B:0 STATUS:DEGRADED
```

### Full Command Reference

| Node | Token | Example |
|---|---|---|
| `utility_a/b` | `STATUS:<state>` | `STATUS:OUTAGE` |
| `utility_a/b` | `VOLT:<kv>` | `VOLT:184.0` |
| `utility_a/b` | `FREQ:<hz>` | `FREQ:58.8` |
| `utility_a/b` | `LOAD:<pct>` | `LOAD:45` |
| `mv_switchgear_a/b` | `CLOSE` / `OPEN` | `CLOSE` |
| `mv_switchgear_a/b` | `LOAD:<pct>` | `LOAD:50` |
| `mv_switchgear_a/b` | `STATUS:<state>` | `STATUS:TRIPPED` |
| `mv_lv_transformer_a/b` | `LOAD:<pct>` | `LOAD:80` |
| `mv_lv_transformer_a/b` | `TEMP:<f>` | `TEMP:175` |
| `mv_lv_transformer_a/b` | `STATUS:<state>` | `STATUS:FAULT` |
| `generator_a/b` | `FUEL:<pct>` | `FUEL:20` |
| `generator_a/b` | `RPM:<rpm>` | `RPM:1800` |
| `generator_a/b` | `LOAD:<pct>` | `LOAD:60` |
| `generator_a/b` | `STATUS:<state>` | `STATUS:RUNNING` |
| `ats_a/b` | `SOURCE:UTILITY\|GENERATOR\|OPEN` | `SOURCE:GENERATOR` |
| `ats_a/b` | `LOAD:<pct>` | `LOAD:40` |
| `ats_a/b` | `STATUS:<state>` | `STATUS:FAULT` |
| `lv_dist_a/b` | `INPUT:<v>` | `INPUT:480` |
| `lv_dist_a/b` | `UPS:<kw>` | `UPS:110.0` |
| `lv_dist_a/b` | `MECH:<kw>` | `MECH:50.0` |
| `lv_dist_a/b` | `SOURCE:<src>` | `SOURCE:GENERATOR` |
| `lv_dist_a/b` | `STATUS:<state>` | `STATUS:FAULT` |
| `ups_a/b` | `BATT:<pct>` | `BATT:15` |
| `ups_a/b` | `LOAD:<pct>` | `LOAD:70` |
| `ups_a/b` | `INPUT:<v>` | `INPUT:0` |
| `ups_a/b` | `STATUS:<state>` | `STATUS:ON_BATTERY` |
| `pdu_a/b` | `INPUT:<v>` | `INPUT:480` |
| `pdu_a/b` | `LOAD:<pct>` | `LOAD:60` |
| `pdu_a/b` | `STATUS:<state>` | `STATUS:OVERLOAD` |
| `rectifier_a/b` | `INPUT_AC:<v>` | `INPUT_AC:480` |
| `rectifier_a/b` | `LOAD:<pct>` | `LOAD:40` |
| `rectifier_a/b` | `STATUS:<state>` | `STATUS:FAULT` |
| `cooling_a/b` | `INPUT:<v>` | `INPUT:480` |
| `cooling_a/b` | `TEMP:<f>` | `TEMP:75` |
| `cooling_a/b` | `SPEED:<pct>` | `SPEED:80` |
| `cooling_a/b` | `STATUS:<state>` | `STATUS:DEGRADED` |
| `lighting_a/b` | `INPUT:<v>` | `INPUT:277` |
| `lighting_a/b` | `DIM:<pct>` | `DIM:50` |
| `lighting_a/b` | `STATUS:<state>` | `STATUS:DIMMED` |
| `monitoring_a/b` | `INPUT:<v>` | `INPUT:120` |
| `monitoring_a/b` | `SENSORS:<n>` | `SENSORS:10` |
| `monitoring_a/b` | `STATUS:<state>` | `STATUS:ALERT` |
| `server_rack` | `CPU:<pct>` | `CPU:90` |
| `server_rack` | `TEMP:<f>` | `TEMP:92` |
| `server_rack` | `UNITS:<n>` | `UNITS:12` |
| `server_rack` | `PATH_A:<0\|1>` | `PATH_A:0` |
| `server_rack` | `PATH_B:<0\|1>` | `PATH_B:1` |
| `server_rack` | `STATUS:<state>` | `STATUS:DEGRADED` |

---

## 8. Data Flow

```
                    ┌──────────────────────────────────────────┐
                    │            ESP32 Nodes                   │
                    │  Telemetry JSON every 5s (QoS 0, retained)│
                    └───────────────────┬──────────────────────┘
                                        │ winter-river/<id>/status
                                        ▼
                    ┌──────────────────────────────────────────┐
                    │        Mosquitto MQTT Broker             │
                    │    192.168.4.1:1883 (TCP)                │
                    │    192.168.4.1:9001 (WebSocket)          │
                    └───────┬──────────────────┬───────────────┘
                            │                  │
               QoS 1 sub    │                  │ QoS 0 sub
                            ▼                  ▼
          ┌─────────────────────┐    ┌─────────────────────┐
          │   broker/main.py    │    │      Telegraf        │
          │  Simulation engine  │    │  MQTT consumer       │
          │  1 Hz tick rate     │    │  10s flush interval  │
          │  PostgreSQL state   │    └──────────┬──────────┘
          │  propagation        │               │
          └─────────┬───────────┘               │
                    │ Control cmds              │ Line protocol
                    │ QoS 1, NOT retained       ▼
                    │               ┌─────────────────────┐
                    │               │  InfluxDB v2         │
                    │               │  bucket: mqtt_metrics│
                    │               │  org: iot-project    │
                    │               └──────────┬──────────┘
                    │                          │ Flux queries
                    │                          ▼
                    │               ┌─────────────────────┐
                    │               │      Grafana         │
                    │               │   port 3000          │
                    │               └─────────────────────┘
                    │
                    │ winter-river/<id>/control
                    ▼
          ┌─────────────────────┐
          │    ESP32 Nodes       │
          │  mqttCallback token  │
          │  parser updates      │
          │  local state vars    │
          └─────────────────────┘
```

---

## 9. NTP Time Synchronization

All ESP32 nodes synchronize time from the Raspberry Pi's local NTP server.

```cpp
// From every node's setup()
configTime(-28800, 3600, "192.168.4.1");  // UTC-8 + 1h DST
```

| Property | Value |
|---|---|
| NTP server | `192.168.4.1` (Pi) |
| NTP daemon | `ntpsec` (Debian Trixie) or `ntp`/`ntpd` (older) |
| Subnet restriction | `192.168.4.0/24` (added by `scripts/setup_pi.sh`) |
| Timestamp format | `HH:MM:SS` (local time, included in all telemetry JSON as `"ts"`) |
| Retry on failure | 10 attempts × 500ms before continuing without sync |

---

## 10. WiFi Transport

| Property | Value |
|---|---|
| SSID | `WinterRiver-AP` |
| Password | `winterriver` |
| Security | WPA-PSK minimum (`WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK)`) |
| Band | **2.4 GHz only** (`bg`, channel 6) — ESP32 has no 5 GHz radio |
| AP gateway | `192.168.4.1` |
| DHCP | dnsmasq (via NetworkManager shared mode), leases at `/var/lib/misc/dnsmasq.leases` |
| Node IP range | `192.168.4.x` (DHCP assigned) |

**WiFi reset sequence (mandatory on all nodes):**
```cpp
WiFi.persistent(false);
WiFi.mode(WIFI_OFF);      delay(200);
WiFi.mode(WIFI_STA);
WiFi.disconnect(false);   delay(200);
WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
WiFi.begin(ssid, password);
// 20s timeout → 30s wait → ESP.restart()
```

---

## 11. Timing Summary

| Component | Interval | Notes |
|---|---|---|
| ESP32 telemetry publish | 5 seconds | `delay(5000)` in `loop()` |
| Simulation engine tick | 1 second | `tick_rate = 1.0` in `config.toml` |
| Generator startup delay | 10 ticks (10s) | `GEN_STARTUP_TICKS = 10` in `main.py` |
| Telegraf collection interval | 10 seconds | `interval = "10s"` in `telegraf.conf` |
| Telegraf flush interval | 10 seconds | `flush_interval = "10s"` |
| MQTT keepalive | 60 seconds | `keepalive = 60` in `config.sample.toml` |
| WiFi connect timeout | 20 seconds | Before restart sequence |
| WiFi restart delay | 30 seconds | Allows Pi hotspot time to come up |

---

## 12. Telegraf → InfluxDB Mapping

Telegraf subscribes to `winter-river/+/status` and parses JSON payloads from the node status topics only.

```toml
[[inputs.mqtt_consumer]]
  servers    = ["tcp://192.168.4.1:1883"]
  topics     = ["winter-river/+/status"]
  client_id  = "telegraf-winter-river"
  qos        = 0
  topic_tag  = "topic"           # full topic stored as tag
  data_format = "json"
  tag_keys   = ["node_id", "status", "type"]
  json_string_fields = ["node_id", "status", "type"]

[[outputs.influxdb_v2]]
  urls         = ["http://localhost:8086"]
  token        = "${INFLUX_TOKEN}"
  organization = "iot-project"
  bucket       = "mqtt_metrics"
```

**Measurement name:** The MQTT topic string (e.g. `winter-river/pdu_a/status`)
**Tags:** `node_id`, `status`, `type` (promoted from JSON fields)
**Fields:** All remaining numeric JSON fields

---

## 13. Manual Testing Commands

```bash
# Subscribe to all node telemetry (live)
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v

# Publish a single control command
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE"

# Publish a compound control command
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "RPM:1800 STATUS:RUNNING"

# Check last retained message for a specific node
mosquitto_sub -h 192.168.4.1 -t "winter-river/utility_a/status" -C 1

# Run full system status check (Pi only)
./scripts/status.sh

# Live MQTT feed (Pi only)
./scripts/status.sh mqtt
```

---

## 14. Known Issues & Mismatches

| Issue | Status | Detail |
|---|---|---|
| Telegraf topic schema | **Fixed** | `telegraf.conf` now subscribes to `winter-river/+/status` (was `iot/node/+/...`) |
| `broker/main.py` DB schema | **Open** | `telemetry_history` table and `live_status.current_state` column referenced in Python but not in `scripts/init_db.sql` — reconcile before running engine against fresh DB |
| `pdu_b` legacy firmware note | **Stale docs removed** | `pdu_b` is part of the active SSD1306/MQTT node matrix and should be treated like `pdu_a` |
| InfluxDB auth token | **Change for production** | Default `my-super-secret-auth-token` in `config.sample.toml` and `grafana/.env.sample` |
