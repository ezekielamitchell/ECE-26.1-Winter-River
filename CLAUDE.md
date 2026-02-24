# CLAUDE.md — ECE 26.1 Winter River

## 1. Project Identity

**Name:** ECE 26.1 Winter River
**What it does:** A modular, tabletop-scale data center power infrastructure simulator using ESP32 IoT nodes, a Raspberry Pi MQTT broker/hotspot, and a Grafana visualization stack — built as an AWS-sponsored senior capstone for Seattle University's College of Science and Engineering.
**Who it's for:** Data center students, new engineers, and operations staff learning power chain topology, redundancy logic, and failure response in a safe hands-on environment.
**Course/Client:** Seattle University ECE Senior Capstone (ECE 26.1), sponsored by Amazon Web Services (AWS).
**Team:** Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma
**Primary developer/maintainer:** Ezekiel A. Mitchell

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Raspberry Pi 5                              │
│                                                                     │
│  ┌─────────────────┐   ┌────────────────┐   ┌──────────────────┐   │
│  │   NetworkManager │   │   Mosquitto    │   │ Docker Stack     │   │
│  │   + hostapd      │   │  MQTT Broker   │   │ Grafana :3000    │   │
│  │  SSID: WinterRiver-AP  port 1883      │   │ InfluxDB :8086   │   │
│  │  192.168.4.1/24  │   │ (0.0.0.0:1883)│   │ Telegraf         │   │
│  └────────┬─────────┘   └───────┬────────┘   └───────┬──────────┘   │
│           │ DHCP leases         │ retained msgs       │ MQTT→InfluxDB│
└───────────┼─────────────────────┼─────────────────────┼─────────────┘
            │ 2.4 GHz WiFi        │ MQTT pub/sub         │ Flux queries
            │ (WPA-PSK)           │ QoS 1, LWT           │
            ▼                     ▼                       ▼
     ┌──────────────────────────────────┐         ┌──────────────┐
     │   ESP32 Nodes (8 active, Side A) │         │   Browser    │
     │                                  │         │  Dashboard   │
     │  util_a → trf_a → sw_a → gen_a  │         └──────────────┘
     │               ↓                  │
     │           dist_a → ups_a         │
     │               ↓                  │
     │           pdu_a → srv_a          │
     │                                  │
     │  Each node:                      │
     │  • Connects to WinterRiver-AP    │
     │  • Publishes telemetry JSON 5s   │
     │  • Subscribes to /control topic  │
     │  • Sets LWT OFFLINE retained msg │
     │  • SSD1306 OLED shows live state │
     └──────────────────────────────────┘

Data flow:
ESP32 → MQTT publish → Mosquitto → Telegraf → InfluxDB → Grafana
                                 → broker/main.py simulation engine
                                   └→ MQTT publish control commands → ESP32
```

NTP flow: Pi serves NTP to all ESP32 nodes at 192.168.4.1 (ntpsec/ntp).

---

## 3. Hardware

| Component | Model | Qty | Notes |
|---|---|---|---|
| Central controller | Raspberry Pi 5 | 1 | Hotspot + MQTT broker + Docker stack |
| WiFi interface | Built-in wlan0 | 1 | Must support AP mode; forced to 2.4 GHz bg ch6 |
| Edge nodes | ESP32 DevKitC v1 (esp32doit-devkit-v1) | 8 active | Dual-core, 240 MHz, 520 KB SRAM, 802.11 b/g/n 2.4 GHz only |
| OLED display | SSD1306 128×64, I2C | 1 per node | Address 0x3C (most modules) or 0x3D (SA0 pin HIGH) |
| ADC (legacy) | ESP32 GPIO 34 | pdu_b only | Analog voltage read, 11dB attenuation, 64-sample averaging |
| PCB baseplate | Custom, USB-C connectors | 1 | Plug-and-play node mounting (in design/fabricated) |

**Critical hardware note:** ESP32 is 2.4 GHz only — the hotspot MUST be on band `bg`, channel 6. 5 GHz will silently prevent all node connections.

**OLED address detection:** `gen_a` and `dist_a` use `detectOLEDAddr()` scanning 0x3C then 0x3D via I2C Wire probe. Other nodes are hardcoded 0x3C and confirmed working. Do not change this without physically verifying the module address.

---

## 4. Project Structure

```
ECE-26.1-Winter-River/
│
├── CLAUDE.md                          # ← This file. Read at session start.
├── README.md                          # Project overview, team, performance targets
├── CONTRIBUTING.md                    # Branching strategy, PR templates, coding standards
├── SUMMARY.md                         # GitBook navigation index
├── LICENSE                            # MIT
├── .gitignore                         # Ignores venv, .env, config.toml, .pio/, build/
├── config.toml                        # Root runtime config (MQTT host, DB URL, tick rate)
│
├── .gitbook/
│   └── assets/                        # WR_1.png, WR_1_B.png (light/dark logo variants)
│
├── .github/
│   └── workflows/
│       └── ci.yml                     # GitHub Actions: python lint + pio build (placeholders)
│
├── broker/                            # Python simulation engine + MQTT bridge
│   ├── main.py                        # WinterRiverEngine: reads MQTT telemetry, runs
│   │                                  #   simulation tick, propagates power hierarchy,
│   │                                  #   publishes control commands back to nodes
│   ├── __init__.py
│   ├── config.sample.toml             # Template — copy to config.toml (git-ignored)
│   ├── pyproject.toml                 # Full dep list incl. psycopg2, sqlalchemy, alembic
│   ├── requirements.txt               # Trimmed runtime: paho-mqtt==1.6.1, toml==0.10.2
│   ├── requirements-dev.txt           # Dev tools: pytest, black, flake8, mypy, isort
│   └── README.md
│
├── deploy/                            # Pi systemd units and service setup scripts
│   ├── mosquitto_setup.sh             # Installs mosquitto, writes /etc/mosquitto/mosquitto.conf,
│   │                                  #   enables + starts service, runs pub/sub smoke test
│   └── winter-river-hotspot.service   # Type=oneshot RemainAfterExit; calls setup_hotspot.sh
│                                      #   start/stop; runs Before=mosquitto.service
│
├── docs/
│   ├── architecture.md                # General IoT architecture (pre-Winter River specifics)
│   └── deployment.md                  # Step-by-step Pi + ESP32 deployment (partially outdated)
│
├── esp32-nodes/                       # All ESP32 PlatformIO firmware
│   ├── platformio.ini                 # Multi-env config; shared [env] base; 8 chain envs +
│   │                                  #   legacy pdu_b/server envs
│   ├── README.md
│   ├── TROUBLESHOOTING.md             # Known hardware/WiFi/display debugging steps
│   ├── include/                       # Shared headers (currently empty)
│   ├── lib/                           # Local libraries (currently empty)
│   ├── test/                          # Unit test stubs (currently empty)
│   └── src/
│       ├── utility/util_a/
│       │   └── util_a.cpp             # ① Root node. 230 kV / 60 Hz / 3-phase grid source.
│       │                              #   States: GRID_OK, SAG, SWELL, OUTAGE, FAULT.
│       │                              #   LWT disconnect triggers full Side-A cascade.
│       │                              #   OLED hardcoded 0x3C.
│       ├── transformer/transformer_a/
│       │   └── transformer_a.cpp      # ② 230 kV→480 V, 500 kVA. Tracks load%, power_kva,
│       │                              #   temp_f. States: NORMAL, WARNING, FAULT.
│       │                              #   OLED hardcoded 0x3C.
│       ├── switchgear/sw_a/
│       │   └── sw_a.cpp               # ③ ATS. 480 V, breaker open/close/trip.
│       │                              #   Tracks current_a, load_kw, load_pct.
│       │                              #   States: CLOSED, OPEN, TRIPPED, FAULT.
│       │                              #   OLED hardcoded 0x3C.
│       ├── generator/gen_a/
│       │   └── gen_a.cpp              # ④ Diesel gen. 480 V output. Tracks fuel_pct, rpm,
│       │                              #   output_v, load_pct.
│       │                              #   States: STANDBY, STARTING, RUNNING, FAULT.
│       │                              #   OLED uses detectOLEDAddr() (0x3C / 0x3D scan).
│       ├── distribution/dist_a/
│       │   └── dist_a.cpp             # ⑤ LV distribution board. 480 V, 800 A, 384 kW rated.
│       │                              #   Tracks ups_load_kw, mech_load_kw, total_load_kw.
│       │                              #   power_source: UTILITY | GENERATOR | NONE.
│       │                              #   States: NORMAL, OVERLOAD, FAULT, NO_INPUT.
│       │                              #   OLED uses detectOLEDAddr() (0x3C / 0x3D scan).
│       ├── ups/ups_a/
│       │   └── ups_a.cpp              # ⑥ UPS. Tracks battery_pct, load_pct, input_v, output_v.
│       │                              #   States: NORMAL, ON_BATTERY, CHARGING, FAULT.
│       │                              #   OLED hardcoded 0x3C.
│       ├── pdu/
│       │   ├── pdu_a/pdu_a.cpp        # ⑦ Active PDU. 480 V. Minimal firmware — no LWT,
│       │   │                          #   no NTP, no mqttCallback. Publishes simple relay msg.
│       │   │                          #   OLED hardcoded 0x3C.
│       │   └── pdu_b/pdu_b.cpp        # LEGACY — still uses LiquidCrystal_I2C (16x2 LCD at
│       │                              #   0x3F) and reads ADC_PIN 34. NOT in active chain.
│       │                              #   Do not use as a template for new nodes.
│       └── server_rack/srv_a/
│           └── srv_a.cpp              # ⑧ Server rack. 208 V. Tracks cpu_load_pct,
│                                      #   inlet_temp_f, power_kw, units_active.
│                                      #   States: NORMAL, THROTTLED, FAULT.
│                                      #   OLED hardcoded 0x3C.
│
├── grafana/                           # Docker monitoring stack
│   ├── docker-compose.yml             # influxdb:2.7, grafana:latest, telegraf:latest
│   ├── grafana.ini                    # Grafana config (port 3000, anon disabled)
│   ├── telegraf.conf                  # MQTT consumer → InfluxDB v2 (topics: iot/node/+/...)
│   │                                  #   NOTE: topics not yet updated to winter-river/# schema
│   ├── .env.sample                    # Copy to grafana/.env; sets passwords + MQTT_BROKER_HOST
│   ├── README.md
│   ├── dashboards/                    # Dashboard JSON exports (currently empty)
│   └── provisioning/
│       ├── dashboards/                # Grafana dashboard provisioning config
│       └── datasources/               # Grafana datasource provisioning config
│
├── images/
│   ├── WR_1.png                       # Logo light variant
│   ├── WR_1_B.png                     # Logo dark variant
│   └── wr_fb_diagram.jpg              # Physical functional block diagram
│
├── scripts/
│   ├── setup_pi.sh                    # Run FIRST (as sudo). Installs mosquitto + ntpsec,
│   │                                  #   runs mosquitto_setup.sh, installs hotspot service,
│   │                                  #   configures NTP subnet access, enables all services.
│   ├── setup_hotspot.sh               # Manages NetworkManager AP connection (start/stop/status).
│   │                                  #   Creates "winter-river-hotspot" NM profile:
│   │                                  #   band=bg, ch=6, WPA-PSK, ipv4.method=shared, 192.168.4.1/24
│   ├── status.sh                      # Live health check: services, hotspot, DHCP clients,
│   │                                  #   MQTT ping, last retained msg per node.
│   │                                  #   `./status.sh mqtt` tails all winter-river/# topics.
│   ├── init_db.sql                    # PostgreSQL schema: nodes, live_status, historical_data.
│   │                                  #   Seeds both Side A and Side B (util_a..srv_b).
│   └── README.md
│
├── examples/                          # Reserved (empty)
├── tests/                             # Reserved (empty)
└── typescript/                        # Reserved — future dashboard/API (empty)
```

---

## 5. Tech Stack

### ESP32 Firmware
| Layer | Detail |
|---|---|
| Language | C++ (Arduino framework) |
| Build system | PlatformIO Core |
| Board | `esp32doit-devkit-v1` |
| Framework | `espressif32` Arduino |
| MQTT client | `knolleary/PubSubClient@^2.8` |
| OLED driver | `adafruit/Adafruit SSD1306@^2.5.7` |
| GFX layer | `adafruit/Adafruit GFX Library@^1.11.5` |
| Serial monitor | 115200 baud |
| WiFi auth | WPA-PSK minimum (`WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK)`) |
| OLED protocol | I2C via Wire library, SDA/SCL default pins |
| Telemetry interval | 5000 ms (hardcoded `delay(5000)` in each loop) |

### Raspberry Pi / Broker
| Layer | Detail |
|---|---|
| OS | Raspberry Pi OS (Debian Trixie confirmed) |
| MQTT broker | Mosquitto (system package), port 1883, anonymous, persistence on |
| WiFi AP | NetworkManager (`nmcli`), hostapd-backed, band=bg, ch=6 |
| DHCP | dnsmasq (via NM shared mode), leases at `/var/lib/misc/dnsmasq.leases` |
| NTP | ntpsec (Trixie) or ntp/ntpd (older), restricted to 192.168.4.0/24 |
| Python version | 3.9+ |
| Python MQTT | paho-mqtt==1.6.1 |
| Simulation DB | PostgreSQL (psycopg2-binary, SQLAlchemy, Alembic — in pyproject.toml) |
| Config format | TOML (`config.toml`, `broker/config.sample.toml`) |

### Monitoring Stack (Docker on Pi or separate host)
| Layer | Detail |
|---|---|
| Container runtime | Docker + Docker Compose v3.8 |
| Time-series DB | InfluxDB 2.7 (bucket: `mqtt_metrics`, org: `iot-project`) |
| Dashboard | Grafana latest (port 3000) |
| MQTT bridge | Telegraf latest (MQTT consumer → InfluxDB v2 output) |
| InfluxDB token | `my-super-secret-auth-token` (must change for production) |

### CI/CD
| Layer | Detail |
|---|---|
| Platform | GitHub Actions |
| Triggers | push to `main`/`ezekielamitchell`, PR to `main` |
| Python checks | lint + format + test (currently placeholder `echo` stubs) |
| PlatformIO build | `cd esp && pio run` (currently commented out placeholder) |

---

## 6. Performance Targets

From README.md (Winter Quarter 2026):
- **6–12 ESP32 nodes** with MQTT connectivity — **Achieved**
- **Custom PCB** design completed and ordered — **Achieved**
- **Firmware architecture** established (base ESP32 template for primaries) — In progress
- **Mosquitto MQTT broker** running on Pi — In progress

Spring Quarter 2026 targets:
- **24 functional modules** (full 2N dual-path Side A + Side B)
- **Full 2N redundancy simulation** with dual power path logic
- **3+ automated failure scenarios** (utility loss + gen startup delay, UPS switchover, cooling failure)
- **Grafana dashboard** deployed with real-time visualization
- **Simulation tick rate:** 1 second (`tick_rate = 1.0` in config.toml)

---

## 7. Key Design Decisions

1. **Raspberry Pi as combined hotspot + MQTT broker.** All ESP32 nodes join the Pi's own WiFi AP (192.168.4.1). No external network required. This makes the system fully self-contained for demos and classrooms.

2. **MQTT LWT (Last Will and Testament) for cascade failure.** Each node sets a retained LWT OFFLINE message at connect time. When `util_a` is unplugged, its LWT fires automatically — the simulation engine detects this and propagates failure down the chain. No polling needed.

3. **Retained MQTT messages for state persistence.** Node status topics are retained. After reconnect or status.sh query, the last known state is immediately available without waiting for the next 5s publish cycle.

4. **One PlatformIO environment per node (`build_src_filter`).** Each node has its own `[env:node_id]` in `platformio.ini` with `build_src_filter = +<component_type/node_id/>`. This allows `pio run -e util_a` to build only that node's firmware, while all nodes share common `lib_deps`.

5. **OLED initialized before WiFi in every `setup()`.** WiFi radio startup causes I2C interference if OLED is initialized after. All nodes call `Wire.begin()` + `display.begin()` before `WiFi.begin()`.

6. **Full WiFi radio reset sequence before `WiFi.begin()`.** Pattern: `WiFi.persistent(false)` → `WiFi.mode(WIFI_OFF)` → `delay(200)` → `WiFi.mode(WIFI_STA)` → `WiFi.disconnect(false)` → `delay(200)` → `WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK)` → `WiFi.begin()`. This clears NVS caches and stale state that otherwise causes silent connection failures.

7. **20-second WiFi timeout then 30-second wait then restart.** If WiFi fails, the node waits 30s (time for Pi hotspot to come up on standalone power) then calls `ESP.restart()`. This enables autonomous recovery without physical intervention.

8. **SSD1306 OLED address scan (`detectOLEDAddr`) only on nodes with confirmed address ambiguity.** `gen_a` and `dist_a` physically have SA0 pulled HIGH, making them 0x3D. Other nodes are confirmed 0x3C. Only apply the scan function where needed — do not add it blindly to all nodes.

9. **pdu_b is legacy.** It still uses `LiquidCrystal_I2C` (16×2 LCD) and ADC voltage reading. It is excluded from the active chain. Do not use it as a template.

10. **Simulation engine runs on Pi in `broker/main.py`.** A 1-second tick loop fetches node states from PostgreSQL, computes top-down power propagation (UTILITY → GENERATOR → UPS → rest), and publishes control commands back to nodes via MQTT `/control` topics. This decouples physics from firmware.

11. **Hotspot must be 2.4 GHz band `bg` channel 6.** `nmcli` does not always accept `--band` in the `device wifi hotspot` shorthand. The setup script uses `nmcli connection add` with explicit `wifi.band bg` and `wifi.channel 6` to guarantee 2.4 GHz. NetworkManager default can pick 5 GHz, which blocks all ESP32 connections.

12. **Mosquitto config fully overwritten by `mosquitto_setup.sh`.** All `conf.d/` drop-ins are deleted to prevent duplicate directive errors (especially `listener` and `persistence_location`). The script owns the entire `/etc/mosquitto/mosquitto.conf`.

---

## 8. API / Protocol Schema

### MQTT Topic Structure

```
winter-river/
├── <node_id>/status     # Telemetry (published by ESP32, retained)
└── <node_id>/control    # Commands (published by simulation engine, consumed by ESP32)
```

Active `<node_id>` values: `util_a`, `trf_a`, `sw_a`, `gen_a`, `dist_a`, `ups_a`, `pdu_a`, `srv_a`

---

### Telemetry Payloads (ESP32 → Broker, every 5s, retained)

**util_a:**
```json
{"ts":"14:32:01","v_out":230.0,"freq_hz":60.0,"load_pct":12,"state":"GRID_OK","voltage_kv":230.0,"phase":3}
```

**trf_a (transformer_a):**
```json
{"ts":"14:32:01","load":45,"power_kva":225.0,"temp_f":108,"status":"NORMAL","voltage":480}
```

**sw_a:**
```json
{"ts":"14:32:01","breaker":true,"current_a":120.5,"load_kw":86.5,"load_pct":35,"state":"CLOSED","voltage":480}
```

**gen_a:**
```json
{"ts":"14:32:01","fuel_pct":85,"rpm":0,"output_v":0.0,"load_pct":0,"state":"STANDBY","voltage":480}
```

**dist_a:**
```json
{"ts":"14:32:01","input_v":480.0,"ups_load_kw":95.0,"mech_load_kw":42.0,"total_load_kw":137.0,"load_pct":36,"source":"UTILITY","state":"NORMAL","voltage":480.0}
```

**ups_a:**
```json
{"ts":"14:32:01","battery_pct":100,"load_pct":40,"input_v":480.0,"output_v":480.0,"state":"NORMAL","voltage":480}
```

**pdu_a:**
```
MQTT Relay Successful: pdu_a
```
*(pdu_a publishes plain string, not JSON — does not use structured telemetry topic)*

**srv_a:**
```json
{"ts":"14:32:01","cpu_pct":42,"inlet_f":75,"power_kw":3.2,"units":8,"state":"NORMAL","voltage":208}
```

---

### LWT / Online Messages (retained on `winter-river/<node_id>/status`)

**Online (published at connect):**
```json
{"ts":"14:32:01","node":"util_a","status":"ONLINE"}
```

**Offline LWT (broker publishes on disconnect):**
```json
{"node":"util_a","status":"OFFLINE"}
```

---

### Control Commands (Broker → ESP32, topic: `winter-river/<node_id>/control`)

| Node | Command format | Example |
|---|---|---|
| util_a | `STATUS:<state>` | `STATUS:OUTAGE` |
| util_a | `VOLT:<kv>` | `VOLT:184.0` |
| util_a | `FREQ:<hz>` | `FREQ:58.8` |
| util_a | `LOAD:<pct>` | `LOAD:45` |
| trf_a | `LOAD:<pct>` | `LOAD:80` |
| trf_a | `TEMP:<f>` | `TEMP:175` |
| trf_a | `STATUS:<state>` | `STATUS:WARNING` |
| sw_a | `OPEN` / `CLOSE` | `OPEN` |
| sw_a | `LOAD:<pct>` | `LOAD:50` |
| sw_a | `STATUS:<state>` | `STATUS:TRIPPED` |
| gen_a | `FUEL:<pct>` | `FUEL:20` |
| gen_a | `RPM:<rpm>` | `RPM:1800` |
| gen_a | `LOAD:<pct>` | `LOAD:60` |
| gen_a | `STATUS:<state>` | `STATUS:RUNNING` |
| dist_a | `INPUT:<v>` | `INPUT:480` |
| dist_a | `UPS:<kw>` | `UPS:110.0` |
| dist_a | `MECH:<kw>` | `MECH:50.0` |
| dist_a | `SOURCE:<src>` | `SOURCE:GENERATOR` |
| dist_a | `STATUS:<state>` | `STATUS:FAULT` |
| ups_a | `BATT:<pct>` | `BATT:15` |
| ups_a | `LOAD:<pct>` | `LOAD:70` |
| ups_a | `INPUT:<v>` | `INPUT:0` |
| ups_a | `STATUS:<state>` | `STATUS:ON_BATTERY` |
| srv_a | `CPU:<pct>` | `CPU:90` |
| srv_a | `TEMP:<f>` | `TEMP:92` |
| srv_a | `UNITS:<n>` | `UNITS:12` |
| srv_a | `STATUS:<state>` | `STATUS:THROTTLED` |

Simulation engine compound command format (from `broker/main.py`):
```
INPUT:480.0 STATUS:NORMAL BATT:100
```

---

## 9. Database Schema

PostgreSQL schema (`scripts/init_db.sql`):

```sql
-- Static topology: who is plugged into whom
nodes (
    node_id       VARCHAR(50) PRIMARY KEY,
    node_type     VARCHAR(20) NOT NULL,   -- UTILITY, TRANSFORMER, SW_GEAR, GENERATOR,
                                          --   DIST_BOARD, UPS, PDU, SERVER_RACK
    side          CHAR(1),                -- 'A' or 'B'
    parent_id     VARCHAR(50) REFERENCES nodes(node_id),
    v_ratio       FLOAT DEFAULT 1.0       -- voltage step-down ratio from parent
)

-- Live "digital twin" state (updated every simulation tick)
live_status (
    node_id       VARCHAR(50) PRIMARY KEY REFERENCES nodes(node_id),
    is_present    BOOLEAN DEFAULT FALSE,
    v_in          FLOAT DEFAULT 0.0,
    v_out         FLOAT DEFAULT 0.0,
    status_msg    VARCHAR(50) DEFAULT 'OFFLINE',
    battery_level INT DEFAULT 100,         -- UPS nodes only
    gen_timer     INT DEFAULT 0,           -- GENERATOR startup countdown ticks
    last_update   TIMESTAMP DEFAULT NOW()
)

-- Historical telemetry log
historical_data (
    id        SERIAL PRIMARY KEY,
    node_id   VARCHAR(50) REFERENCES nodes(node_id),
    timestamp TIMESTAMP DEFAULT NOW()
    -- NOTE: metrics column appears in broker/main.py as JSON but not in init_db.sql
    -- broker/main.py also references a "telemetry_history" table not in init_db.sql
)
```

**Seed data** inserts both Side A and Side B nodes (util_a through srv_b, 16 total) with v_ratio values:
- trf_a: 0.15 (230 kV × 0.15 ≈ 34.5 kV — note: simulation approximation)
- sw_a: 0.014 (scale factor for display, not physical)
- srv_a: 0.1

**Schema mismatch warning:** `broker/main.py` references `telemetry_history (node_id, metrics JSON)` and `live_status.current_state` / `live_status.v_out` — these columns are NOT in `init_db.sql`. The SQL schema and Python engine are not fully synchronized. When updating either, update both.

---

## 10. Development Workflow

### Flash a single ESP32 node

```bash
cd esp32-nodes
pio run -e util_a --target upload    # build + flash
pio device monitor                   # serial monitor at 115200
```

### Build all nodes without flashing

```bash
cd esp32-nodes
pio run    # builds all environments defined in platformio.ini
```

### Build a specific node only

```bash
pio run -e trf_a
pio run -e sw_a
pio run -e gen_a
# etc.
```

### Check live system status (run on Pi)

```bash
./scripts/status.sh          # full report
./scripts/status.sh mqtt     # live MQTT topic tail
```

### Subscribe to all node telemetry manually

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
```

### Publish a control command manually

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/util_a/control" -m "STATUS:OUTAGE"
```

### Start/stop the Pi hotspot manually

```bash
sudo ./scripts/setup_hotspot.sh start
sudo ./scripts/setup_hotspot.sh stop
sudo ./scripts/setup_hotspot.sh status
```

### Start/stop Mosquitto

```bash
sudo systemctl start mosquitto
sudo systemctl stop mosquitto
sudo journalctl -u mosquitto -n 50
```

### Run the Python simulation engine

```bash
cd broker
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python main.py
```

### Start the Grafana monitoring stack

```bash
cd grafana
cp .env.sample .env           # edit .env first
docker-compose up -d
docker-compose ps
docker-compose logs -f telegraf
```

### Stop Grafana stack

```bash
cd grafana
docker-compose down
```

### Initialize PostgreSQL schema

```bash
psql -U postgres -d sensor_data -f scripts/init_db.sql
```

---

## 11. Build Order

Bring the full system up from scratch in this order:

1. **Provision the Raspberry Pi (once only)**
   ```bash
   sudo ./scripts/setup_pi.sh
   ```
   This installs mosquitto + ntpsec, configures the hotspot service, sets NTP subnet access, and enables all services for autoboot.

2. **Verify Pi services are running**
   ```bash
   ./scripts/status.sh
   ```
   Expect: `winter-river-hotspot: OK`, `mosquitto: RUNNING`, `ntp: RUNNING`

3. **Flash each ESP32 node** (from development machine with USB)
   ```bash
   cd esp32-nodes
   pio run -e util_a --target upload
   pio run -e transformer_a --target upload
   pio run -e sw_a --target upload
   pio run -e gen_a --target upload
   pio run -e dist_a --target upload
   pio run -e ups_a --target upload
   pio run -e pdu_a --target upload
   pio run -e srv_a --target upload
   ```
   Flash in chain order (util_a first) to observe cascade behavior from the start.

4. **Power on ESP32 nodes.** Each node will:
   - Connect to `WinterRiver-AP`
   - Sync NTP from 192.168.4.1
   - Connect to MQTT broker at 192.168.4.1:1883
   - Begin publishing telemetry every 5 seconds

5. **Verify node telemetry**
   ```bash
   ./scripts/status.sh         # check retained messages per node
   ./scripts/status.sh mqtt    # live feed
   ```

6. **Start simulation engine (optional)**
   ```bash
   cd broker && source venv/bin/activate && python main.py
   ```

7. **Start Grafana stack (optional)**
   ```bash
   cd grafana && docker-compose up -d
   # Access at http://192.168.4.1:3000
   ```

---

## 12. Claude Code Guidelines

### Version control
- **NEVER run any git commands.** The developer handles all version control manually. Do not `git add`, `git commit`, `git push`, `git pull`, `git stash`, or any other git operation.

### ESP32 firmware conventions
- All nodes use `Adafruit SSD1306` OLED — **never** `LiquidCrystal_I2C` (except `pdu_b` which is legacy and not a template).
- OLED must be initialized before WiFi in `setup()` to prevent I2C interference.
- The full WiFi reset sequence (`WIFI_OFF → delay → WIFI_STA → disconnect → setMinSecurity → begin`) is mandatory on all nodes. Do not simplify.
- All new nodes must set a **LWT** retained message on `winter-river/<node_id>/status` at connect time, and publish an ONLINE override after connecting.
- All new nodes must subscribe to `winter-river/<node_id>/control` and implement `mqttCallback`.
- Use `delay(5000)` at the end of `loop()` — telemetry interval is 5 seconds across all nodes.
- NTP: always include `configTime(-28800, 3600, "192.168.4.1")` and a `getTimestamp()` helper.
- New nodes go in `esp32-nodes/src/<component_type>/<node_id>/<node_id>.cpp`. Add a matching `[env:<node_id>]` with `build_src_filter = +<<component_type>/<node_id>/>` to `platformio.ini`.
- Do not apply `detectOLEDAddr()` to nodes that are confirmed working at 0x3C. Only use it on new nodes where the I2C address is uncertain.

### Python broker conventions
- `broker/main.py` is the simulation engine. MQTT topics follow `winter-river/<node_id>/status` and `winter-river/<node_id>/control`.
- Keep `broker/requirements.txt` minimal (runtime only: paho-mqtt, toml). Full dep list is in `pyproject.toml`.
- Configuration is TOML — never hardcode credentials. Use `config.toml` (git-ignored) copied from `config.sample.toml`.

### Mosquitto / deploy
- Never edit `/etc/mosquitto/mosquitto.conf` directly — it is owned by `deploy/mosquitto_setup.sh`. Re-run the script to update it.
- The hotspot NetworkManager connection name is always `winter-river-hotspot`. The SSID is `WinterRiver-AP`, password `winterriver`, gateway `192.168.4.1`.

### Database
- Keep `scripts/init_db.sql` in sync with `broker/main.py` table references. Currently they diverge (`telemetry_history`, `current_state` column).

### Grafana / Telegraf
- Telegraf `telegraf.conf` still subscribes to `iot/node/+/...` topics — this needs updating to `winter-river/<node_id>/status` to match actual firmware. Do not change without also updating Grafana datasource queries.

---

## 13. Common Pitfalls

1. **Hotspot on 5 GHz = all nodes fail to connect silently.** Always verify hotspot band with `nmcli -g 802-11-wireless.band connection show winter-river-hotspot` → must be `bg`.

2. **OLED blank screen after SSD1306 conversion.** `display.begin(SSD1306_SWITCHCAPVCC, 0x3C)` returns false silently if device doesn't ACK at 0x3C. No error, no display, no indication. Use `detectOLEDAddr()` or verify the physical I2C address with an I2C scanner sketch first.

3. **LiquidCrystal_I2C copy-assign corruption** (already fixed, do not reintroduce). Never do `lcd = LiquidCrystal_I2C(addr, 16, 2)` in `setup()` — the class has no copy-assign operator and silently corrupts the internal I2C handle. If LCD is ever needed, use heap allocation: `LiquidCrystal_I2C *lcd = nullptr; lcd = new LiquidCrystal_I2C(addr, 16, 2);`.

4. **WiFi status check without reset = stale connection.** Without the full `WIFI_OFF → WIFI_STA → disconnect` sequence, ESP32 may silently fail to connect to the hotspot even though it reports connecting. Always use the established reset pattern.

5. **Mosquitto `conf.d/` drop-in files cause "duplicate directive" crashes.** If mosquitto fails to start after any apt upgrade that reinstalls the package, check `/etc/mosquitto/conf.d/` for default files. Re-run `deploy/mosquitto_setup.sh` which deletes all drop-ins and rewrites the main config.

6. **`pdu_b` still has `LiquidCrystal_I2C` in its source** but the library is removed from `platformio.ini` `lib_deps`. Building `[env:pdu_b]` will fail to link. Either add the library back or ignore the pdu_b environment — it is not in the active chain.

7. **`status.sh` shows `winter-river-hotspot: STOPPED`** even when the hotspot is working. This happens when the Pi hasn't pulled the latest `status.sh` (which handles `Type=oneshot` with `RemainAfterExit=yes` by checking `SubState=exited`). Fix with `git pull` on the Pi.

8. **NTP sync failure.** If ESP32 reports "NTP failed", check that `ntpsec` (or `ntp`) is running on the Pi AND that the NTP config has `restrict 192.168.4.0 mask 255.255.255.0 nomodify notrap`. This line is added by `setup_pi.sh` but may be missing on manual setups.

9. **MQTT broker not accepting connections from nodes.** Check: (a) Mosquitto config has `listener 1883 0.0.0.0` (not `127.0.0.1`). (b) `allow_anonymous true` is set. (c) Hotspot is up and nodes have 192.168.4.x IPs. (d) No firewall blocking port 1883.

10. **`broker/main.py` references tables not in `init_db.sql`.** `telemetry_history` and `live_status.current_state` / `live_status.v_out` are used in Python but not defined in the SQL file. Running the engine against a freshly initialized DB will throw `psycopg2.errors.UndefinedTable`. Reconcile before running the engine.

11. **Grafana Telegraf topics mismatch.** `telegraf.conf` subscribes to `iot/node/+/telemetry` but all firmware publishes to `winter-river/<node_id>/status`. No data will flow into InfluxDB until `telegraf.conf` is updated.

---

## 14. File Naming Conventions

| Pattern | Convention | Examples |
|---|---|---|
| Node source files | `<node_id>/<node_id>.cpp` | `util_a/util_a.cpp`, `gen_a/gen_a.cpp` |
| Node directories | `<component_type>/<node_id>/` | `utility/util_a/`, `generator/gen_a/` |
| Component type dirs | lowercase, singular | `utility/`, `transformer/`, `switchgear/`, `generator/`, `distribution/`, `ups/`, `pdu/`, `server_rack/` |
| Node IDs | `<abbrev>_<side>` | `util_a`, `trf_a`, `sw_a`, `gen_a`, `dist_a`, `ups_a`, `pdu_a`, `srv_a` |
| PlatformIO env names | match node_id | `[env:util_a]`, `[env:transformer_a]` |
| MQTT topics | `winter-river/<node_id>/<type>` | `winter-river/util_a/status`, `winter-river/gen_a/control` |
| Scripts | `snake_case.sh` | `setup_pi.sh`, `setup_hotspot.sh`, `status.sh` |
| Python files | `snake_case.py` | `main.py` |
| Config templates | `<name>.sample.<ext>` | `config.sample.toml`, `.env.sample` |
| Systemd units | `<project>-<component>.service` | `winter-river-hotspot.service` |
| SQL files | `init_<scope>.sql` | `init_db.sql` |
| Side designation | `_a` suffix = Side A, `_b` = Side B | `util_a`, `util_b` |
