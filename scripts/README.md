# Scripts

Utility scripts for setting up and configuring the Winter River IoT system on Raspberry Pi.

## Contents

| File | Description |
|------|-------------|
| `setup_pi.sh` | Initial Raspberry Pi setup (packages, Python venv, Mosquitto, Grafana, systemd) |
| `setup_hotspot.sh` | Create a local WiFi access point for ESP32 nodes using NetworkManager |
| `init_db.sql` | Database schema for the digital twin (node topology and live status tables) |

## Usage

### Pi Setup

```bash
sudo ./setup_pi.sh
```

Walks through the full system setup: package installation, Python virtual environment, MQTT broker configuration, and Grafana stack deployment. Steps are currently stubbed with TODOs -- uncomment and customize as needed.

### WiFi Hotspot

```bash
sudo ./setup_hotspot.sh          # Start hotspot
sudo ./setup_hotspot.sh stop     # Stop hotspot
sudo ./setup_hotspot.sh status   # Show hotspot status
```

Creates an access point on `wlan0` with the following defaults:

| Setting | Value |
|---------|-------|
| SSID | `WinterRiver-AP` |
| Password | `winterriver` |
| Gateway | `192.168.4.1` |
| DHCP range | `192.168.4.x` |

Reference: [Raspberry Pi Hotel WiFi Hotspot Tutorial](https://www.raspberrypi.com/tutorials/host-a-hotel-wifi-hotspot/)

### Database Init

```bash
psql -U <user> -d <database> -f init_db.sql
```

Creates two tables:

- **`nodes`** -- network topology defining parent-child relationships and voltage ratios
- **`live_status`** -- current digital twin state (presence, voltage in/out, status message)

Seeds initial data for a utility node (`util_a`) and transformer node (`trans_a`).
