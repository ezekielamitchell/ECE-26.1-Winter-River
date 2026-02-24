# ECE 26.1 Winter River

<div align="center" data-with-frame="true"><figure><picture><source srcset=".gitbook/assets/WR_1_B.png" media="(prefers-color-scheme: dark)"><img src=".gitbook/assets/WR_1.png" alt=""></picture><figcaption><p>image [1] logo</p></figcaption></figure></div>

<p align="center"><strong>Seattle University - College of Science and Engineering</strong><br>sponsored by: <strong>Amazon Web Services (AWS)</strong></p>

<p align="center">Team: Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma<br>{lgonzalez1, tlam, wmcdonald, emitchell4, kverma1}@seattleu.edu</p>

<p align="center"><a href="https://github.com/ezekielamitchell/GUARDEN/blob/main/LICENSE/README.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a> <a href="https://www.espressif.com/en/products/socs/esp32-c3"><img src="https://img.shields.io/badge/platform-ESP32--C3-green.svg" alt="Platform"></a> <a href="https://www.python.org/downloads/"><img src="https://img.shields.io/badge/python-3.8+-blue.svg" alt="Python"></a></p>

***

## Overview

[Winter River](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/) ([ezekielamitchell.gitbook.io/ece-26.1-winter-river/](https://ezekielamitchell.gitbook.io/ece-26.1-winter-river/)) is a modular, tabletop-scale data center training simulator designed to bridge the critical skills gap in data center operations. Developed in partnership with Amazon Web Services (AWS), this educational platform provides hands-on experience with data center infrastructure systems without the cost, complexity, or risk associated with real facilities.

The project addresses a growing industry need: as of 2025, the U.S. operates over 5,400 active data centers with data center employment growing 60% from 2016 to 2023, yet qualified personnel continue to fall short of demand. More than half of U.S. data center operators report difficulty hiring qualified candidates, particularly for specialized skills in power distribution, thermal management, and infrastructure systems.

Winter River provides a physical, interactive learning environment where students, new engineers, and operations staff can experiment with data center configurations, observe system interdependencies, and practice emergency response scenarios in a safe, controlled setting. Each component module simulates real data center equipment, from utility power and generators to transformers, UPS units, PDUs, and server racks, creating an accurate representation of 2N redundancy power distribution architectures based on the Open Compute Project topology.

The system leverages embedded IoT architecture with ESP32 microcontrollers in each component, MQTT communication protocols, and a Raspberry Pi 5 central controller to enable real-time simulation of power flow, thermal conditions, and system failures. Visual feedback through OLED displays on each component and an optional dashboard interface provides immediate understanding of system states and cascading effects.

By combining physical modularity (plug-and-play components on a custom PCB baseplate), realistic simulation logic, and scenario-based training capabilities, Winter River transforms abstract data center concepts into tangible, memorable learning experiences that accelerate competency development and reduce operational risk.

***

## Key Features

* Modular Plug-and-Play Architecture: Custom PCB baseplate with USB-C connectors at each node location enables quick reconfiguration of data center topologies. Components attach and detach like breadboard circuits, supporting experimentation with different redundancy configurations and power paths.
* ESP32-Based Smart Components: Each data center component (generators, transformers, UPS, PDUs, server racks) contains an ESP32-WROOM-32 microcontroller with an integrated OLED display showing real-time operational parameters (voltage, current, power consumption, temperature, fault status).
* MQTT Publish-Subscribe Communication: Industry-standard MQTT protocol enables scalable, low-bandwidth communication between all components. ESP32 nodes publish sensor data and receive commands through a central Mosquitto broker, mirroring real industrial IoT architectures.
* Raspberry Pi 5 Simulation Engine: Central controller calculates and broadcasts system-wide power flow, thermal conditions, and failure propagation in real-time. Implements 2N redundancy logic, cumulative rack loading calculations, and coordinated failure scenarios.
* Open Compute Project Topology: Implements 2N redundancy power distribution architecture following OCP Open Rack V3 specifications. Dual power paths from utility/generator through transformers, switchgear, UPS units, and PDUs to server racks provide realistic redundancy training.
* Scenario-Based Training: Automated failure scenarios including utility power loss with generator startup delays, UPS switchover events, cooling system failures with progressive thermal warnings, overload conditions leading to circuit breaker trips, and component removal/hot-swap detection.

***

## Technical Specifications

| Component              | Specification                  | Details                                                                         |
| ---------------------- | ------------------------------ | ------------------------------------------------------------------------------- |
| Microcontroller        | ESP-32 Development Board USB-C | Dual-core, WiFi/BLE, integrated OLED display interface                          |
| Central Controller     | Raspberry Pi 5                 | Simulation engine, Mosquitto MQTT broker, system-wide calculations              |
| Communication Protocol | MQTT Publish-Subscribe         | Industry-standard, scalable, low-bandwidth IoT communication                    |
| Display                | OLED per component             | Real-time operational parameters (voltage, current, power, temperature, status) |
| PCB Architecture       | Custom modular baseplate       | USB-C connectors at each node, plug-and-play components                         |
| Power Topology         | 2N Redundancy                  | OCP Open Rack V3 specifications, dual power paths                               |
| Component Types        | 15-20 modules (stretch 30)     | Generators, transformers, switchgear, UPS units, PDUs, server racks             |
| Simulation Features    | Real-time system states        | Power flow, thermal modeling, failure propagation, hot-swap detection           |
| Development Platform   | PlatformIO + Python            | ESP32 firmware, Raspberry Pi controller scripts, GitHub CI/CD                   |
| Visualization          | Grafana Dashboard              | Real-time metrics, system topology view, historical data analysis               |

***

## Performance Targets

### Winter Quarter 2026

<table><thead><tr><th>Metric</th><th>Target</th><th>Status<select><option value="sqgq1MesM8XL" label="Achieved" color="blue"></option><option value="83KQpvtBIYxJ" label="In progress" color="blue"></option><option value="cdfReAEhJSDI" label="Planned" color="blue"></option></select></th></tr></thead><tbody><tr><td>Proof-of-concept nodes operational</td><td>6-12 ESP32 nodes with MQTT</td><td><span data-option="sqgq1MesM8XL">Achieved</span></td></tr><tr><td>PCB design completed and ordered</td><td>Custom power distribution PCB</td><td><span data-option="sqgq1MesM8XL">Achieved</span></td></tr><tr><td>Firmware architecture established</td><td>Base ESP32 template code for primaries</td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td>Raspberry Pi broker configured</td><td>Mosquitto MQTT running</td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td>Database configuration</td><td><p></p><p></p></td><td></td></tr></tbody></table>

### Spring Quarter 2026

<table><thead><tr><th>Metric</th><th>Target</th><th>Status<select><option value="sqgq1MesM8XL" label="Achieved" color="blue"></option><option value="83KQpvtBIYxJ" label="In progress" color="blue"></option><option value="cdfReAEhJSDI" label="Planned" color="blue"></option></select></th></tr></thead><tbody><tr><td>Component modules completed</td><td>24 functional modules</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Full 2N redundancy simulation</td><td>Dual power path logic</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Failure scenarios implemented</td><td>3+ automated scenarios</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Dashboard deployed</td><td>Grafana real-time visualization</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Documentation complete</td><td>User + technical manuals</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>AWS delivery</td><td>Functional prototype delivered</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr></tbody></table>

***

## Project Structure

```md
ECE-26.1-Winter-River/
├── README.md
├── CONTRIBUTING.md
├── SUMMARY.md                         # GitBook navigation index
├── LICENSE
├── .gitignore
├── .gitbook/
│   └── assets/
├── .github/
│   └── workflows/
├── broker/                            # Python MQTT broker utilities
├── deploy/                            # Raspberry Pi systemd units & setup
│   ├── mosquitto_setup.sh             # ① Configure Mosquitto (TCP 1883 + WS 9001)
│   └── winter-river-hotspot.service   # ② Register Pi as 2.4 GHz access point
├── docs/
├── esp32-nodes/                       # PlatformIO firmware for all ESP32 nodes
│   ├── platformio.ini
│   └── src/
│       ├── utility/        → util_a   # ① 230 kV MV grid feed — chain root
│       ├── transformer/    → trf_a    # ② 230 kV → 480 V step-down
│       ├── switchgear/     → sw_a     # ③ ATS — utility / generator transfer
│       ├── generator/      → gen_a    # ④ Backup diesel generator, 480 V
│       ├── distribution/   → dist_a   # ⑤ LV distribution board, 384 kW
│       ├── ups/            → ups_a    # ⑥ UPS — battery %, charge state
│       ├── pdu/            → pdu_a    # ⑦ Rack PDU, 480 V
│       └── server_rack/    → srv_a    # ⑧ Server rack endpoint
├── grafana/                           # Grafana + InfluxDB + Telegraf (native systemd)
│   ├── telegraf.conf                  # Telegraf MQTT → InfluxDB bridge config
│   ├── provisioning/                  # Auto-provisioned datasources & dashboards
│   ├── dashboards/                    # Dashboard JSON definitions
│   └── .env.sample                    # Credential template (copy → .env)
├── images/
├── scripts/
│   ├── setup_pi.sh                    # ① Run first — full Pi provisioning
│   └── status.sh                      # ② Live node & service health check
└── typescript/

```

### Power Chain

<table><thead><tr><th width="108.19921875">#</th><th width="129.421875">Node</th><th width="330.63671875">Role</th><th>Voltage</th></tr></thead><tbody><tr><td>①</td><td><code>util_a</code></td><td>MV utility grid feed (chain root)</td><td>230 kV</td></tr><tr><td>②</td><td><code>trf_a</code></td><td>Step-down transformer</td><td>230 kV → 480 V</td></tr><tr><td>③</td><td><code>sw_a</code></td><td>Automatic transfer switch (ATS)</td><td>480 V</td></tr><tr><td>④</td><td><code>gen_a</code></td><td>Backup diesel generator</td><td>480 V</td></tr><tr><td>⑤</td><td><code>dist_a</code></td><td>LV distribution board</td><td>480 V, 384 kW</td></tr><tr><td>⑥</td><td><code>ups_a</code></td><td>Uninterruptible power supply</td><td>480 V</td></tr><tr><td>⑦</td><td><code>pdu_a</code></td><td>Rack power distribution unit</td><td>480 V</td></tr><tr><td>⑧</td><td><code>srv_a</code></td><td>Server rack endpoint</td><td>480 V</td></tr></tbody></table>

### Key Configuration Files

| File                                  | Purpose                                                       |
| ------------------------------------- | ------------------------------------------------------------- |
| `esp32-nodes/platformio.ini`          | PlatformIO build environments (one per node)                  |
| `deploy/mosquitto_setup.sh`           | Configures Mosquitto (TCP 1883 + WebSocket 9001)              |
| `deploy/winter-river-hotspot.service` | Systemd unit for the Pi access point                          |
| `scripts/setup_pi.sh`                 | Run first — provisions the entire Pi stack end-to-end         |
| `scripts/status.sh`                   | Checks all 8 nodes + Pi services at a glance                  |
| `grafana/telegraf.conf`               | Telegraf config — MQTT consumer → InfluxDB v2 bridge          |
| `grafana/.env.sample`                 | Credential template; copy to `grafana/.env` before setup      |
| `grafana/provisioning/`               | Auto-provisioned Grafana datasources and dashboard configs    |

<br>
