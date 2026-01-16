---
layout:
  width: wide
  title:
    visible: true
  description:
    visible: false
  tableOfContents:
    visible: true
  outline:
    visible: true
  pagination:
    visible: true
  metadata:
    visible: true
---

# ECE 26.1 Winter River

<div align="center" data-with-frame="true"><figure><picture><source srcset=".gitbook/assets/guarden_dark.png" media="(prefers-color-scheme: dark)"><img src=".gitbook/assets/guarden.png" alt="" width="375"></picture><figcaption><p>image [1] logo</p></figcaption></figure></div>

<p align="center"><strong>Seattle University - College of Science and Engineering</strong><br>sponsored by:<br><strong>Amazon Web Services (AWS)</strong></p>

<p align="center">Team: Leilani Gonzalez, Ton Dam Lam (Adam), William McDonald, Ezekiel A. Mitchell, Keshav Verma<br>{lgonzalez1, tlam, wmcdonald, emitchell4, kverma1}@seattleu.edu</p>

<p align="center"><a href="https://github.com/ezekielamitchell/GUARDEN/blob/main/LICENSE/README.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a> <a href="https://www.espressif.com/en/products/socs/esp32-c3"><img src="https://img.shields.io/badge/platform-ESP32--C3-green.svg" alt="Platform"></a> <a href="https://www.python.org/downloads/"><img src="https://img.shields.io/badge/python-3.8+-blue.svg" alt="Python"></a></p>

***

## Overview



***

## Key Features

*

***

## Technical Specifications

<table data-full-width="true"><thead><tr><th>Component</th><th>Specification</th><th>Performance</th><th>Power</th></tr></thead><tbody><tr><td><strong>Processor</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Camera</strong></td><td></td><td></td><td></td></tr><tr><td><strong>AI Model</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Motion Sensor</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Sleep Mode</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Connectivity</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Storage</strong></td><td></td><td></td><td></td></tr><tr><td><strong>Power</strong></td><td></td><td></td><td></td></tr></tbody></table>

***

## Performance Targets

<table><thead><tr><th>Metric</th><th>Target</th><th>Status<select><option value="sqgq1MesM8XL" label="Achieved" color="blue"></option><option value="83KQpvtBIYxJ" label="In progress" color="blue"></option></select></th></tr></thead><tbody><tr><td></td><td></td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td></td><td></td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td></td><td></td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td></td><td></td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr><tr><td></td><td></td><td><span data-option="83KQpvtBIYxJ">In progress</span></td></tr></tbody></table>

***

## Project Structure

```
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
