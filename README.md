# ECE 26.1 Winter River

<div align="center">
  <picture>
    <source srcset=".gitbook/assets/WR_1_B.png" media="(prefers-color-scheme: dark)">
    <img src="images/WR_1.png" alt="Winter River Logo">
  </picture>
</div>

<p align="center"><strong>Seattle University — College of Science and Engineering</strong><br>Sponsored by: <strong>Amazon Web Services (AWS)</strong></p>

<p align="center">
  Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma<br>
  {lgonzalez1, tlam, wmcdonald, emitchell4, kverma1}@seattleu.edu
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <a href="https://www.espressif.com/en/products/socs/esp32"><img src="https://img.shields.io/badge/platform-ESP32--WROOM--32-green.svg" alt="Platform"></a>
  <a href="https://www.python.org/downloads/"><img src="https://img.shields.io/badge/python-3.9+-blue.svg" alt="Python"></a>
  <a href="https://platformio.org/"><img src="https://img.shields.io/badge/firmware-PlatformIO-orange.svg" alt="PlatformIO"></a>
</p>

---

## Overview

[Winter River](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/) is a modular, tabletop-scale data center training simulator designed to bridge the critical skills gap in data center operations. Developed at Seattle University in partnership with Amazon Web Services (AWS), this educational platform provides hands-on experience with data center infrastructure without the cost, complexity, or risk of real facilities.

The project addresses a growing industry need: as of 2025, the U.S. operates over 5,400 active data centers with data center employment growing 60% from 2016–2023, yet qualified personnel continue to fall short of demand. More than half of U.S. data center operators report difficulty hiring qualified candidates, particularly for specialized skills in power distribution, thermal management, and infrastructure systems.

Winter River provides a physical, interactive learning environment where students, new engineers, and operations staff can experiment with data center configurations, observe system interdependencies, and practice emergency response scenarios in a safe, controlled setting. It implements a full **2N redundancy** Open Compute Project power distribution topology across 25 ESP32-based component nodes connected via MQTT to a Raspberry Pi 5 simulation engine.

---

## Key Features

- **Modular plug-and-play architecture** — Custom PCB baseplate with USB-C connectors at each node location for quick reconfiguration of topologies
- **25 smart component nodes** — Each ESP32-WROOM-32 node has an SSD1306 OLED display showing real-time voltage, state, and fault status
- **MQTT pub/sub communication** — Industry-standard Mosquitto broker on TCP 1883 (nodes) and WebSocket 9001 (Grafana Live)
- **Topology-aware simulation engine** — Python broker uses Kahn's BFS topological sort to propagate cascade failures through the power chain in proper dependency order
- **2N redundancy** — Dual power paths (Side A + Side B) feeding a shared `server_rack` node; degraded-mode operation when one path fails
- **Generator startup delay** — 10-tick (~10 second) startup simulation; UPS bridges the gap during utility loss
- **Battery tracking** — UPS nodes track charge level per tick (1%/tick charge/discharge), transitioning through CHARGING → NORMAL → ON_BATTERY states
- **Real-time monitoring** — Telegraf bridges MQTT telemetry to InfluxDB v2; Grafana dashboards visualize all 25 nodes
- **Scenario-based training** — Utility outages, UPS switchovers, cooling faults, and component hot-swap detection

---

## Technical Specifications

| Component | Specification | Details |
|---|---|---|
| Microcontroller | ESP32-WROOM-32 | Dual-core, 2.4 GHz WiFi, per-node SSD1306 OLED |
| Central Controller | Raspberry Pi 5 | MQTT broker, simulation engine, monitoring stack |
| Communication | MQTT (Mosquitto) | TCP 1883 (nodes), WebSocket 9001 (Grafana) |
| Simulation Engine | Python 3.9+ | Kahn's BFS topo sort, 1 Hz tick rate |
| Firmware | C++ (Arduino) + PlatformIO | 25 build environments |
| Time-Series DB | InfluxDB 2 + Telegraf | MQTT consumer → InfluxDB pipeline |
| Visualization | Grafana | Auto-provisioned dashboards at `:3000` |
| Relational DB | PostgreSQL | Node topology, live status, historical data |
| Power Topology | 2N Redundancy | OCP Open Rack V3 — 25 nodes (12A + 12B + 1 shared) |
| Networking | WiFi Hotspot | `WinterRiver-AP` 2.4 GHz, DHCP `192.168.4.x` |

---

## Repository Structure

```
ECE-26.1-Winter-River/
├── README.md
├── CONTRIBUTING.md
├── SUMMARY.md                         # GitBook navigation index
├── LICENSE                            # MIT License
├── config.toml                        # Runtime config (git-ignored; copy from broker/config.sample.toml)
│
├── .github/
│   └── workflows/
│       └── ci.yml                     # GitHub Actions: Python lint + PlatformIO build
│
├── broker/                            # Python simulation engine
│   ├── main.py                        # WinterRiverEngine — topo sort, cascade logic, InfluxDB writes
│   ├── config.sample.toml             # Template → copy to root config.toml
│   ├── requirements.txt               # Runtime: paho-mqtt, toml, psycopg2, influxdb-client
│   ├── requirements-dev.txt           # Dev: pytest, black, flake8, mypy
│   └── pyproject.toml                 # Packaging, linting, and type-check config
│
├── deploy/                            # Raspberry Pi setup
│   ├── mosquitto_setup.sh             # Configures Mosquitto (TCP 1883, WS 9001)
│   └── winter-river-hotspot.service   # Systemd unit for WiFi access point
│
├── docs/                              # Technical documentation
│   ├── architecture.md                # System layers, component roles, communication
│   ├── deployment.md                  # Pi setup, firmware flashing, troubleshooting
│   ├── communication-protocols.md     # MQTT topics, payloads, QoS, LWT, timing
│   ├── 2n-redundancy.md               # Dual-path power topology details
│   └── ECEGR4880 Technical Report.pdf # Full technical report
│
├── esp32-nodes/                       # PlatformIO firmware for all 25 ESP32 nodes
│   ├── platformio.ini                 # 25 active environments + legacy section
│   ├── TROUBLESHOOTING.md             # WiFi, MQTT, and OLED debugging guide
│   └── src/
│       ├── utility/                   # ①  230 kV grid feed (Side A + B chain roots)
│       ├── mv_switchgear/             # ②  34.5 kV main disconnect & protection relay
│       ├── mv_lv_transformer/         # ③  34.5 kV → 480 V, 1000 kVA step-down
│       ├── generator/                 # ④  Backup diesel gen (480 V, 10-tick startup delay)
│       ├── ats/                       # ⑤  Automatic transfer switch (dual-source)
│       ├── lv_dist/                   # ⑥  LV distribution board (480 V, 384 kW)
│       ├── ups/                       # ⑦  UPS — battery %, charge state, ON_BATTERY mode
│       ├── pdu/                       # ⑧  Rack PDU (480 V AC)
│       ├── rectifier/                 # ⑨  AC→DC rectifier (480 V AC → 48 V DC HVDC)
│       ├── cooling/                   # ⑩  CRAC/CRAH cooling unit
│       ├── lighting/                  # ⑪  277 V lighting circuit
│       ├── monitoring/                # ⑫  120 V DCIM/BMS monitoring equipment
│       └── server_rack/               # ⑬  2N shared rack endpoint (48 V DC)
│
├── grafana/                           # Monitoring stack configuration
│   ├── grafana.ini                    # Grafana server settings
│   ├── telegraf.conf                  # MQTT consumer → InfluxDB v2 bridge
│   ├── .env.sample                    # Credentials template → copy to grafana/.env
│   ├── dashboards/
│   │   ├── broker-overview.json       # MQTT broker metrics dashboard
│   │   └── nodes-telemetry.json       # ESP32 node telemetry dashboard
│   └── provisioning/                  # Auto-provisioned datasources & dashboards
│
├── images/                            # Project photos and logos
├── scripts/
│   ├── setup_pi.sh                    # Full Pi bootstrap — run this first (idempotent)
│   ├── setup_hotspot.sh               # WiFi AP start/stop/status
│   ├── status.sh                      # Live node & service health check
│   └── init_db.sql                    # PostgreSQL schema with 25-node seed data
│
└── typescript/                        # Reserved for future web dashboard/API
```

---

## Power Chain & Node Topology

### Side A — 12 Nodes

| # | Node ID | Role | Voltage |
|---|---------|------|---------|
| ① | `utility_a` | MV utility grid feed (chain root) | 230 kV |
| ② | `mv_switchgear_a` | Main disconnect & protection relay | 34.5 kV |
| ③ | `mv_lv_transformer_a` | Step-down transformer | 34.5 kV → 480 V |
| ④ | `generator_a` | Backup diesel generator (ATS secondary) | 480 V |
| ⑤ | `ats_a` | Automatic transfer switch (prefers transformer) | 480 V |
| ⑥ | `lv_dist_a` | LV distribution board — IT + mechanical loads | 480 V, 384 kW |
| ⑦ | `ups_a` | Uninterruptible power supply | 480 V AC |
| ⑧ | `pdu_a` | Rack power distribution unit | 480 V AC |
| ⑨ | `rectifier_a` | AC→DC rectifier (HVDC path) | 480 V AC → 48 V DC |
| ⑩ | `cooling_a` | CRAC/CRAH cooling unit | 480 V AC |
| ⑪ | `lighting_a` | Lighting circuit | 277 V AC |
| ⑫ | `monitoring_a` | DCIM / BMS monitoring systems | 120 V AC |

Side B mirrors Side A exactly with `_b` suffix on all node IDs.

### Shared Node

| Node ID | Role | Parents |
|---------|------|---------|
| `server_rack` | 2N redundant server rack endpoint | `rectifier_a` (primary) + `rectifier_b` (secondary) |

### 2N Redundancy Logic

| `rectifier_a` | `rectifier_b` | `server_rack` State |
|---|---|---|
| live | live | `NORMAL` (48 V) |
| live | dead | `DEGRADED` (48 V) |
| dead | live | `DEGRADED` (48 V) |
| dead | dead | `FAULT` (0 V) |

### Power Flow Diagram

```
utility_a (230 kV)
  └─ mv_switchgear_a (34.5 kV)
       └─ mv_lv_transformer_a (480 V)
            ├─ generator_a (480 V backup) ──┐
            └─ ats_a (480 V) ←──────────────┘
                 └─ lv_dist_a (480 V, 384 kW)
                      ├─ ups_a → pdu_a → rectifier_a (48 V DC) ──┐
                      ├─ cooling_a                                  ├── server_rack
                      ├─ lighting_a           rectifier_b (48 V) ──┘
                      └─ monitoring_a
                           [Side B mirrors above]
```

---

## Simulation Engine (`broker/main.py`)

The `WinterRiverEngine` is the central brain of the simulator (~500 lines of Python).

### Data Flow

```
ESP32 nodes  →  MQTT :1883  →  Mosquitto  →  paho-mqtt (broker/main.py)
                                                │
                                     Topological sort (Kahn's BFS)
                                                │
                                     Compute each node's state
                                                │
                              ┌─────────────────┴─────────────────┐
                    MQTT control commands                    InfluxDB writes
                    (→ ESP32 nodes, QoS 1)              (optional, direct)
                                                │
                                       PostgreSQL (live_status)
```

### Node State Handlers

Each of the 13 node types has dedicated logic in `_compute_node()`:

| Node Type | Key Behavior |
|---|---|
| `UTILITY` | Root node — `GRID_OK` returns 230 kV; `OUTAGE`/`FAULT` returns 0 |
| `MV_SWITCHGEAR` | Passes parent voltage through when energized |
| `MV_LV_TRANSFORMER` | Scales parent voltage by `v_ratio` |
| `GENERATOR` | 10-tick startup delay on utility loss; `STARTING` → `RUNNING` |
| `ATS` | Prefers transformer input; falls back to generator |
| `LV_DIST` | Distributes voltage to all children |
| `UPS` | Battery tracking: charges (+1%/tick), discharges (-1%/tick) |
| `PDU` | Passes upstream voltage to rectifier |
| `RECTIFIER` | Converts 480 V AC → 48 V DC |
| `COOLING` | State driven by upstream power; tracks coolant temp |
| `LIGHTING` | Passes 277 V when energized |
| `MONITORING` | Passes 120 V when energized |
| `SERVER_RACK` | 2N: NORMAL/DEGRADED/FAULT based on both rectifier paths |

### Topological Sort

`_topo_sort()` uses Kahn's BFS to guarantee parents are computed before children. It handles dual-parent nodes (`parent_id` + `secondary_parent_id`) so the `server_rack`'s NORMAL/DEGRADED/FAULT state is always computed after both `rectifier_a` and `rectifier_b`.

---

## MQTT Communication

**Broker:** Mosquitto running on the Raspberry Pi (`192.168.4.1:1883`)

### Topics

| Topic | Direction | Format | Interval |
|---|---|---|---|
| `winter-river/<node_id>/status` | ESP32 → Broker | JSON | 5 seconds (retained) |
| `winter-river/<node_id>/control` | Broker → ESP32 | `KEY:value KEY:value` | On state change |

### Example Telemetry Payloads

```json
// utility_a
{"ts": "14:32:01", "state": "GRID_OK", "v_out": 230000, "freq_hz": 60.0, "load_pct": 72}

// generator_a
{"ts": "14:32:01", "state": "RUNNING", "rpm": 1800, "fuel_pct": 94, "output_v": 480}

// ups_a
{"ts": "14:32:01", "state": "ON_BATTERY", "battery_pct": 87, "load_pct": 65, "input_v": 0, "output_v": 480}

// server_rack
{"ts": "14:32:01", "state": "DEGRADED", "path_a": 1, "path_b": 0}
```

### Example Control Commands

```
STATUS:OUTAGE                              # Force utility outage
RPM:1800 STATUS:RUNNING                    # Start generator
INPUT:480.0 BATT:87 STATUS:ON_BATTERY      # UPS on battery
PATH_A:1 PATH_B:0 STATUS:DEGRADED          # Server rack degraded
```

---

## ESP32 Firmware

All node firmware follows a shared C++ template (Arduino framework, built with PlatformIO):

- **WiFi** — Full radio reset + reconnect sequence; 20s timeout triggers restart
- **MQTT** — LWT (`OFFLINE`, retained, QoS 1) + `ONLINE` override on connect; 5s status publishes; token-based command parser for compound control messages
- **OLED** — SSD1306 128×64 via I2C (auto-detects 0x3C/0x3D); 4-line real-time display
- **NTP** — Syncs from Pi at `192.168.4.1`, UTC-8 + DST

### Build & Flash

```bash
cd esp32-nodes

# Build all 25 environments
pio run

# Build and flash a single node
pio run -e utility_a --target upload
pio run -e server_rack --target upload

# Monitor serial output
pio device monitor -e utility_a
```

Available environments: `utility_a/b`, `mv_switchgear_a/b`, `mv_lv_transformer_a/b`, `generator_a/b`, `ats_a/b`, `lv_dist_a/b`, `ups_a/b`, `pdu_a/b`, `rectifier_a/b`, `cooling_a/b`, `lighting_a/b`, `monitoring_a/b`, `server_rack`

---

## Monitoring Stack

All services run natively on the Raspberry Pi via systemd (no Docker).

| Service | Port | Purpose |
|---|---|---|
| Mosquitto | 1883 (TCP), 9001 (WS) | MQTT broker |
| InfluxDB 2 | 8086 | Time-series storage (bucket: `mqtt_metrics`) |
| Telegraf | — | MQTT consumer → InfluxDB bridge |
| Grafana | 3000 | Dashboard visualization |
| PostgreSQL | 5432 | Node topology + live status |
| NTP (ntpsec) | 123 | Time server for ESP32 nodes |

**Access Grafana:** `http://192.168.4.1:3000` (while connected to `WinterRiver-AP`)

Auto-provisioned dashboards:
- **Broker Overview** — MQTT broker metrics
- **Nodes Telemetry** — All 25 node states, voltages, and events

---

## Quick Start

### 1. Provision the Raspberry Pi

```bash
# Run the full idempotent bootstrap script
sudo ./scripts/setup_pi.sh

# Configure credentials
cp grafana/.env.sample grafana/.env
# Edit grafana/.env with real passwords

# Verify all services
systemctl status influxdb telegraf grafana-server mosquitto
./scripts/status.sh
```

### 2. Configure the Broker

```bash
cp broker/config.sample.toml config.toml
# Edit config.toml — set MQTT host, PostgreSQL DSN, InfluxDB token
```

### 3. Initialize the Database

```bash
psql -U postgres -f scripts/init_db.sql
```

### 4. Start the Simulation Engine

```bash
cd broker
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python main.py
```

### 5. Flash the ESP32 Nodes

```bash
cd esp32-nodes
pio run -e <node_id> --target upload
```

### 6. Connect & Monitor

- Join the `WinterRiver-AP` WiFi hotspot
- Open `http://192.168.4.1:3000` in a browser
- Nodes appear live as they connect and publish telemetry

---

## Configuration Files

| File | Purpose | Git-tracked? |
|---|---|---|
| `broker/config.sample.toml` | Runtime config template | Yes |
| `config.toml` | Active runtime config (MQTT, DB, InfluxDB) | **No** |
| `grafana/.env.sample` | Credentials template | Yes |
| `grafana/.env` | Active credentials | **No** |
| `esp32-nodes/platformio.ini` | 25 PlatformIO build environments | Yes |
| `scripts/init_db.sql` | PostgreSQL schema + 25-node seed data | Yes |
| `grafana/telegraf.conf` | Telegraf MQTT → InfluxDB bridge config | Yes |

---

## PostgreSQL Schema

```sql
nodes (
  node_id             TEXT PRIMARY KEY,
  node_type           TEXT,
  side                TEXT,           -- 'A', 'B', or NULL
  parent_id           TEXT,
  secondary_parent_id TEXT,           -- for 2N dual-parent nodes (server_rack)
  rated_voltage       REAL,
  v_ratio             REAL
)

live_status (
  node_id       TEXT PRIMARY KEY,
  is_present    BOOLEAN,
  v_in          REAL,
  v_out         REAL,
  status_msg    TEXT,
  battery_level INTEGER,              -- UPS nodes only
  gen_timer     INTEGER,              -- GENERATOR startup countdown
  last_update   TIMESTAMP
)

historical_data (
  node_id    TEXT,
  timestamp  TIMESTAMP,
  metrics    JSONB
)
```

---

## Performance Targets

### Winter Quarter 2026

| Metric | Target | Status |
|---|---|---|
| Proof-of-concept nodes operational | 6–12 ESP32 nodes with MQTT | ✅ Achieved |
| PCB design completed and ordered | Custom power distribution PCB | ✅ Achieved |
| Firmware architecture established | Base ESP32 template for all primaries | ✅ Achieved |
| Mosquitto MQTT broker running on Pi | TCP 1883, anonymous, persistence | ✅ Achieved |
| 24-node firmware written | All 25 envs in platformio.ini | ✅ Achieved |
| Simulation engine (`broker/main.py`) | Topological sort + cascade logic | ✅ Achieved |
| PostgreSQL schema (25 nodes) | `secondary_parent_id`, 2N support | ✅ Achieved |

### Spring Quarter 2026

| Metric | Target | Status |
|---|---|---|
| Full 2N redundancy hardware | 25 physical ESP32 nodes on PCB baseplate | Planned |
| 3+ automated failure scenarios | Utility loss, UPS switchover, cooling fault | Planned |
| Grafana dashboard deployed | Real-time visualization at `:3000` | Planned |
| InfluxDB / Telegraf integration | MQTT → InfluxDB live pipeline | Planned |
| Documentation complete | User + technical manuals | Planned |
| AWS delivery | Functional prototype delivered | Planned |

---

## Development

### Python Broker

```bash
cd broker
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
pip install -r requirements-dev.txt   # pytest, black, flake8, mypy

# Lint
black .
flake8 .

# Type check
mypy main.py
```

### CI/CD

GitHub Actions (`.github/workflows/ci.yml`) runs Python lint checks and PlatformIO builds on every push.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

---

## Documentation

Full documentation is available at [ezekielamitchell.gitbook.io/ece-26.1-winter-river](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/).

| Document | Location | Contents |
|---|---|---|
| Architecture | `docs/architecture.md` | System layers, component roles, communication |
| Deployment | `docs/deployment.md` | Pi setup, firmware flashing, troubleshooting |
| Communication Protocols | `docs/communication-protocols.md` | MQTT topics, payloads, QoS, LWT, timing |
| 2N Redundancy | `docs/2n-redundancy.md` | Dual-path power topology details |
| Firmware Troubleshooting | `esp32-nodes/TROUBLESHOOTING.md` | WiFi, MQTT, OLED debugging |
| Technical Report | `docs/ECEGR4880 Technical Report.pdf` | Full academic report |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
