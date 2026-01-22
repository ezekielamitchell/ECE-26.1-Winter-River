
# <div align="center" data-with-frame="true">ECE 26.1 Winter River</div>

<div align="center" data-with-frame="true"><figure><picture><source srcset=".gitbook/assets/WR_1_B.png" media="(prefers-color-scheme: dark)"><img src=".gitbook/assets/WR_1.png" alt=""></picture><figcaption><p>image [1] logo</p></figcaption></figure></div>

<p align="center"><strong>Seattle University - College of Science and Engineering</strong><br>sponsored by:<br><strong>Amazon Web Services (AWS)</strong></p>

<p align="center">Team: Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma<br>{lgonzalez1, tlam, wmcdonald, emitchell4, kverma1}@seattleu.edu</p>

<p align="center"><a href="https://github.com/ezekielamitchell/GUARDEN/blob/main/LICENSE/README.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a> <a href="https://www.espressif.com/en/products/socs/esp32-c3"><img src="https://img.shields.io/badge/platform-ESP32--C3-green.svg" alt="Platform"></a> <a href="https://www.python.org/downloads/"><img src="https://img.shields.io/badge/python-3.8+-blue.svg" alt="Python"></a> <a href="https://github.com/ezekielamitchell/ECE-26.1-Winter-River/actions/workflows/ci.yml"> <img src="https://github.com/ezekielamitchell/ECE-26.1-Winter-River/actions/workflows/ci.yml/badge.svg" alt="CI"> </a> <a href="./LICENSE"></p>
***

## Overview

Winter River is a modular, tabletop-scale data center training simulator designed to bridge the critical skills gap in data center operations. Developed in partnership with Amazon Web Services (AWS), this educational platform provides hands-on experience with data center infrastructure systems without the cost, complexity, or risk associated with real facilities.

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

<table><thead><tr><th>Metric</th><th>Target</th><th>Status<select><option value="sqgq1MesM8XL" label="Achieved" color="blue"></option><option value="83KQpvtBIYxJ" label="In progress" color="blue"></option><option value="cdfReAEhJSDI" label="Planned" color="blue"></option></select></th></tr></thead><tbody><tr><td>Proof-of-concept nodes operational</td><td>2-3 ESP32 nodes with MQTT</td><td><span data-option="sqgq1MesM8XL">Achieved</span></td></tr><tr><td>PCB design completed and ordered</td><td>Custom power distribution PCB</td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td>Firmware architecture established</td><td>Base ESP32 template code for primaries</td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td>Raspberry Pi broker configured</td><td>Mosquitto MQTT running</td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr></tbody></table>

### Spring Quarter 2026

<table><thead><tr><th>Metric</th><th>Target</th><th>Status<select><option value="sqgq1MesM8XL" label="Achieved" color="blue"></option><option value="83KQpvtBIYxJ" label="In progress" color="blue"></option><option value="cdfReAEhJSDI" label="Planned" color="blue"></option></select></th></tr></thead><tbody><tr><td>Component modules completed</td><td>15-20 functional modules</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Full 2N redundancy simulation</td><td>Dual power path logic</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Failure scenarios implemented</td><td>3+ automated scenarios</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Dashboard deployed</td><td>Grafana real-time visualization</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>Documentation complete</td><td>User + technical manuals</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr><tr><td>AWS delivery</td><td>Functional prototype delivered</td><td><span data-option="cdfReAEhJSDI">Planned</span></td></tr></tbody></table>

***

## Project Structure

```md
ECE-26.1-Winter-River/
├── .github/
│   └── workflows/
│       └── ci.yml                    # CI/CD pipeline
├── broker/
│   ├── __init__.py                    # Python package init
│   ├── config.sample.toml             # Configuration template
│   ├── main.py                        # MQTT broker main entry
│   ├── pyproject.toml                 # Python project config
│   ├── requirements-dev.txt           # Development dependencies
│   ├── requirements.txt               # Production dependencies
│   └── README.md                      # Broker documentation
├── deploy/
│   ├── mosquitto_setup.sh             # Mosquitto MQTT setup script
│   └── mqtt-broker.service            # Systemd service file
├── docs/
│   ├── architecture.md                # System architecture docs
│   └── deployment.md                  # Deployment guide
├── esp32-nodes/
│   ├── include/                       # Header files
│   ├── src/                           # ESP32 source code
│   ├── test/                          # Unit tests
│   ├── .gitignore                     # Git ignore for ESP32
│   └── platformio.ini                 # PlatformIO configuration
├── grafana/
│   ├── dashboards/                    # Grafana dashboard definitions
│   ├── provisioning/                  # Auto-provisioning configs
│   ├── .env.sample                    # Environment variables template
│   ├── docker-compose.yml             # Grafana + data source containers
│   ├── grafana.ini                    # Grafana configuration
│   ├── telegraf.conf                  # Telegraf metrics collector
│   └── README.md                      # Grafana setup documentation
├── scripts/
│   └── setup_pi.sh                    # Raspberry Pi setup script
├── .gitignore                         # Root git ignore
├── CONTRIBUTING.md                    # Contribution guidelines
├── LICENSE                            # MIT license
└── README.md                          # Project overview
```
