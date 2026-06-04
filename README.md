# ECE 26.1 Winter River

<div align="center" data-with-frame="true"><figure><picture><source srcset=".gitbook/assets/WR_1_B.png" media="(prefers-color-scheme: dark)"><img src=".gitbook/assets/WR_1.png" alt=""></picture><figcaption><p>image [1] logo</p></figcaption></figure></div>

<p align="center"><strong>Seattle University - College of Science and Engineering</strong><br>sponsored by: <strong>Amazon Web Services (AWS)</strong></p>

<p align="center">Team: Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma<br>{lgonzalez1, tlam, wmcdonald, emitchell4, kverma1}@seattleu.edu</p>

<p align="center"><a href="./LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a> <a href="https://www.espressif.com/en/products/socs/esp32"><img src="https://img.shields.io/badge/platform-ESP32-green.svg" alt="Platform"></a> <a href="https://www.python.org/downloads/"><img src="https://img.shields.io/badge/python-3.9+-blue.svg" alt="Python"></a></p>

***

## Overview

[Winter River](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/) ([ezekielamitchell.gitbook.io/ece-26.1-winter-river/](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/)) is a modular, tabletop-scale data center training simulator designed to bridge the critical skills gap in data center operations. Developed in partnership with Amazon Web Services (AWS), this educational platform provides hands-on experience with data center infrastructure systems without the cost, complexity, or risk associated with real facilities.

The project addresses a growing industry need: as of 2025, the U.S. operates over 5,400 active data centers with data center employment growing 60% from 2016 to 2023, yet qualified personnel continue to fall short of demand. More than half of U.S. data center operators report difficulty hiring qualified candidates, particularly for specialized skills in power distribution, thermal management, and infrastructure systems.

Winter River provides a physical, interactive learning environment where students, new engineers, and operations staff can experiment with data center configurations, observe system interdependencies, and practice emergency response scenarios in a safe, controlled setting. Each component module simulates real data center equipment — from utility power and generators through HV/MV and MV/LV transformers, their downstream switchgear (the LV switchgear doubling as the utility↔generator transfer point), UPS units, and 8 single-fed server racks — creating an accurate representation of **block-redundant 2N** power distribution where Side A and Side B are two fully independent chains feeding 4 racks each.

The system leverages embedded IoT architecture with ESP32 microcontrollers in each component, MQTT communication protocols, and a Raspberry Pi 5 central controller to enable real-time simulation of power flow, thermal conditions, and system failures. Visual feedback through OLED displays on each component and an optional dashboard interface provides immediate understanding of system states and cascading effects.

By combining physical modularity (plug-and-play components on a custom PCB baseplate), realistic simulation logic, and scenario-based training capabilities, Winter River transforms abstract data center concepts into tangible, memorable learning experiences that accelerate competency development and reduce operational risk.

***

## Key Features

* Modular Plug-and-Play Architecture: Custom PCB baseplate with USB-C connectors at each node location enables quick reconfiguration of data center topologies. Components attach and detach like breadboard circuits, supporting experimentation with different redundancy configurations and power paths.
* ESP32-Based Smart Components: Each data center component (generators, switchgear, transformers, UPS, server racks) contains an ESP32-WROOM-32 microcontroller with an integrated OLED display showing real-time operational parameters (voltage, current, power consumption, temperature, fault status).
* MQTT Publish-Subscribe Communication: Industry-standard MQTT protocol enables scalable, low-bandwidth communication between all components. ESP32 nodes publish sensor data and receive commands through a central Mosquitto broker, mirroring real industrial IoT architectures.
* Raspberry Pi 5 Simulation Engine: Central controller calculates and broadcasts system-wide power flow, thermal conditions, and failure propagation in real-time. Implements 2N redundancy logic, cumulative rack loading calculations, and coordinated failure scenarios.
* Block-Redundant 2N Topology: Two fully independent power chains (Side A and Side B), each feeding 4 single-sided server racks. Side-A failure kills all 4 side-A racks while side-B continues — modelling the side/block redundancy used in many real hyperscale halls.
* Scenario-Based Training: Automated failure scenarios including utility power loss with generator startup delays, UPS switchover events, cooling system failures with progressive thermal warnings, overload conditions leading to circuit breaker trips, and component removal/hot-swap detection.

***

## Technical Specifications

| Component              | Specification                                 | Details                                                                                                                       |
| ---------------------- | --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Microcontroller        | ESP-32 Development Board USB-C                | Dual-core, WiFi/BLE, integrated OLED display interface                                                                        |
| Central Controller     | Raspberry Pi 5                                | Simulation engine, Mosquitto MQTT broker, system-wide calculations                                                            |
| Communication Protocol | MQTT Publish-Subscribe                        | Industry-standard, scalable, low-bandwidth IoT communication                                                                  |
| Display                | OLED per component                            | Real-time operational parameters (voltage, current, power, temperature, status)                                               |
| PCB Architecture       | Custom modular baseplate                      | USB-C connectors at each node, plug-and-play components                                                                       |
| Power Topology         | Block-redundant 2N                            | Two independent side chains; rack-level redundancy lives at the side (block) level                                            |
| Component Types        | 24 modules (12 Side A + 12 Side B)            | Utility, HV/MV transformer, MV switchgear, MV/LV transformer, LV switchgear (utility↔generator transfer point), generator, UPS, cooling, 4× server_rack per side. |
| Simulation Features    | Real-time system states                       | Power flow, thermal modeling, failure propagation, hot-swap detection                                                         |
| Development Platform   | PlatformIO + Python                           | ESP32 firmware, Raspberry Pi controller scripts, GitHub CI/CD                                                                 |
| Visualization          | Grafana Dashboard                             | Real-time metrics, system topology view, historical data analysis                                                             |

***

## Performance Targets

### Winter Quarter 2026

| Metric                               | Target                                | Status     |
| ------------------------------------ | ------------------------------------- | ---------- |
| Proof-of-concept nodes operational   | 6–12 ESP32 nodes with MQTT            | ✅ Achieved |
| PCB design completed and ordered     | Custom power distribution PCB         | ✅ Achieved |
| Firmware architecture established    | Shared `winter_river` ESP32 helper + active node matrix | ✅ Achieved |
| Mosquitto MQTT broker running on Pi  | port 1883, anonymous, persistence     | ✅ Achieved |
| Node firmware matrix written         | All active envs in `platformio.ini`   | ✅ Achieved |
| Simulation engine (`broker/main.py`) | Topological sort + cascade logic      | ✅ Achieved |
| PostgreSQL schema (24 nodes)         | `secondary_parent_id`, block-redundant 2N | ✅ Achieved |

### Spring Quarter 2026

| Metric                          | Target                                      | Status     |
| ------------------------------- | ------------------------------------------- | ---------- |
| Full block-redundant 2N hardware| 24 physical ESP32 nodes (exact fit on 24-slot baseplate) | 🔲 Planned |
| 3+ automated failure scenarios  | Utility loss, UPS switchover, cooling fault | 🔲 Planned |
| Grafana dashboard deployed      | Real-time visualization at :3000            | 🔲 Planned |
| InfluxDB / Telegraf integration | MQTT → InfluxDB live pipeline               | 🔲 Planned |
| Documentation complete          | User + technical manuals                    | 🔲 Planned |
| AWS delivery                    | Functional prototype delivered              | 🔲 Planned |

***

## Project Structure

```
ECE-26.1-Winter-River/
├── README.md
├── CLAUDE.md                          # Comprehensive developer docs (topology, schemas, pitfalls)
├── TESTING.md                         # System test checklist + scenario runbook
├── CONTRIBUTING.md
├── SUMMARY.md                         # GitBook navigation index
├── LICENSE
├── .gitignore
├── .gitbook/
│   └── assets/
├── .github/
│   └── workflows/
│       └── ci.yml                     # GitHub Actions: pytest + pio build matrix
├── broker/                            # Python simulation engine + MQTT bridge
│   ├── main.py                        # WinterRiverEngine: topo sort, cascade logic, InfluxDB writes
│   ├── thermal.py                     # Pure thermal/airflow/PUE model (Capstone-aligned)
│   ├── config.sample.toml             # Reference template; copy to broker/config.toml
│   ├── requirements.txt               # Runtime: paho-mqtt, toml, psycopg2-binary, influxdb-client
│   └── requirements-dev.txt           # Dev: pytest, black, flake8, mypy
├── deploy/                            # Raspberry Pi systemd units & setup
│   ├── mosquitto_setup.sh             # Configures Mosquitto (TCP 1883, anonymous, persistence)
│   └── winter-river-hotspot.service   # Systemd unit — Pi 2.4 GHz access point
├── docs/
│   ├── ECEGR4880 Technical Report.pdf # Capstone formal deliverable
│   └── trainee-assessment.md          # Printable post-workshop test for non-technical trainees
├── esp32-nodes/                       # PlatformIO firmware for all ESP32 nodes
│   ├── platformio.ini                 # Active build environments
│   ├── lib/
│   │   └── winter_river/              # Shared WiFi/MQTT/NTP/OLED/topic helper library
│   └── src/
│       ├── utility/                   # ①  230 kV grid feed — Side A + B chain roots
│       ├── hv_mv_transformer/         # ②  230 kV → 34.5 kV step-down (first on-site)
│       ├── mv_switchgear/             # ③  34.5 kV MV-bus disconnect (downstream of HV/MV xfmr)
│       ├── mv_lv_transformer/         # ④  34.5 kV → 480 V step-down
│       ├── lv_switchgear/             # ⑤  480 V LV bus + utility↔generator transfer point
│       ├── generator/                 # ⑥  Backup diesel gen, 480 V — feeds lv_switchgear secondary
│       ├── ups/                       # ⑦  UPS — battery %, charge state, ON_BATTERY
│       ├── cooling/                   # ⑧  CRAC/CRAH — fan bank (55 fans/side, 110 total)
│       └── server_rack/               # ⑨-⑫ Four 48 V DC racks per side (single shared source + build_flags)
├── grafana/                           # Docker monitoring stack
│   ├── docker-compose.yml             # InfluxDB 2.7 + Grafana + Telegraf
│   ├── telegraf.conf                  # MQTT consumer → InfluxDB v2 bridge
│   ├── provisioning/                  # Auto-provisioned datasources & dashboards
│   ├── dashboards/                    # Dashboard JSON exports
│   └── .env.sample                    # Credential template (copy → grafana/.env)
├── images/
├── scripts/
│   ├── setup_pi.sh                    # Run first — full Pi provisioning
│   ├── setup_hotspot.sh               # Start/stop/status for NetworkManager AP
│   ├── status.sh                      # Live node & service health check
│   └── init_db.sql                    # PostgreSQL schema (24 nodes seeded)
└── tests/                             # pytest suites: broker engine + thermal model
```

### Firmware Architecture

The active ESP32 firmware now shares a common local PlatformIO library at `esp32-nodes/lib/winter_river/`. That helper centralizes WiFi bring-up, MQTT reconnect/LWT handling, NTP time sync, OLED initialization, topic formatting, token parsing, and shared display rows so each node file only owns its local state, control-token handling, display body, and telemetry payload.

This keeps the node matrix consistent across both power paths while making schema updates and bug fixes much cheaper to apply repo-wide.

### Power Chain (Side A — 12 nodes)

| #    | Node                     | Role                                             | Voltage                    |
| ---- | ------------------------ | ------------------------------------------------ | -------------------------- |
| ①    | `utility_a`              | 230 kV utility grid feed (chain root)            | 230 kV                     |
| ②    | `hv_mv_transformer_a`    | HV/MV step-down 230 kV → 34.5 kV (first on-site) | 34.5 kV out                |
| ③    | `mv_switchgear_a`        | 34.5 kV MV-bus breaker (downstream of HV/MV xfmr)| 34.5 kV                    |
| ④    | `mv_lv_transformer_a`    | MV/LV step-down 34.5 kV → 480 V                  | 480 V out                  |
| ⑤    | `lv_switchgear_a`        | 480 V LV bus + utility↔generator transfer point  | 480 V                      |
| ⑥    | `generator_a`            | Backup diesel generator (lv_switchgear secondary)| 480 V                      |
| ⑦    | `ups_a`                  | Uninterruptible power supply (feeds 4 racks)     | 480 V AC                   |
| ⑧    | `cooling_a`              | CRAC/CRAH fan bank (55 fans on this side)        | 480 V AC                   |
| ⑨-⑫ | `server_rack_a{1..4}`    | 48 V DC IT racks (single-fed from `ups_a`)       | 48 V DC                    |

Side B mirrors Side A exactly with `_b` suffix on all node IDs (12 nodes per side, 24 total). There are no shared nodes — sides are fully independent.

### Broker-synthesized

`facility/status` and `weather/status` are published by `broker/main.py` every tick from live node state — no ESP32 firmware and no DB row.

### Key Configuration Files

| File                                  | Purpose                                                              |
| ------------------------------------- | -------------------------------------------------------------------- |
| `esp32-nodes/platformio.ini`          | PlatformIO build environments                                        |
| `broker/config.sample.toml`           | Reference template for runtime config structure                      |
| `scripts/init_db.sql`                 | PostgreSQL schema (24-node seed data, `secondary_parent_id` = LV switchgear's generator feed) |
| `deploy/mosquitto_setup.sh`           | Configures Mosquitto (TCP 1883, anonymous, persistence on)           |
| `deploy/winter-river-hotspot.service` | Systemd unit for the Pi 2.4 GHz AP                                   |
| `scripts/setup_pi.sh`                 | Run first — provisions the entire Pi stack end-to-end                |
| `scripts/status.sh`                   | Checks all nodes + Pi services at a glance                           |
| `grafana/telegraf.conf`               | Telegraf config — MQTT consumer → InfluxDB v2 bridge                 |
| `grafana/.env.sample`                 | Credential template; copy to `grafana/.env` before setup             |

<br>
