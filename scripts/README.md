# Scripts

Utility scripts for setting up and configuring the Winter River IoT system on Raspberry Pi.

## Contents

| File | Description |
|------|-------------|
| `setup_pi.sh` | Full Raspberry Pi bootstrap — packages, Python venv, Mosquitto, WiFi hotspot, NTP, systemd service, Grafana |
| `setup_hotspot.sh` | Create a 2.4 GHz WiFi access point for ESP32 nodes using NetworkManager |
| `init_db.sql` | Database schema for the digital twin (node topology and live status tables) |

---

## Usage

### Full Pi Setup

Run once on a fresh Raspberry Pi after cloning the repo to `/home/pi/ECE-26.1-Winter-River`:

```bash
sudo ./scripts/setup_pi.sh
```

Performs end-to-end setup in this order:

1. `apt update && apt upgrade` + installs: `git`, `python3`, `python3-venv`, `mosquitto`, `mosquitto-clients`, `docker.io`, `docker-compose`, `ntp`
2. Creates `.venv` and runs `pip install -r broker/requirements.txt`
3. Copies `broker/config.sample.toml` → `broker/config.toml` if not present
4. Calls `deploy/mosquitto_setup.sh` to configure and start Mosquitto
5. Calls `scripts/setup_hotspot.sh` to bring up the WiFi AP
6. Configures the system NTP daemon to serve time to the `192.168.4.0/24` subnet (used by ESP32 nodes)
7. Installs and enables the `mqtt-broker` systemd service
8. Starts the Grafana/InfluxDB/Telegraf Docker stack

---

### WiFi Hotspot

```bash
sudo ./setup_hotspot.sh          # Start hotspot
sudo ./setup_hotspot.sh stop     # Stop hotspot
sudo ./setup_hotspot.sh status   # Show status and connected clients
```

Creates an access point on `wlan0` with the following configuration:

| Setting | Value |
|---------|-------|
| SSID | `WinterRiver-AP` |
| Password | `winterriver` |
| Gateway / MQTT broker | `192.168.4.1` |
| Band | **2.4 GHz (802.11 b/g)** |
| Channel | **6** |
| DHCP range | `192.168.4.x` |

> **Why 2.4 GHz is forced:** The ESP32-WROOM-32 only supports 2.4 GHz (802.11 b/g/n). Without explicitly setting `wifi.band bg` and `wifi.channel 6`, NetworkManager may default to 5 GHz, causing nodes to show zero signal bars and fail to connect. The script uses `nmcli connection add` directly (not `nmcli device wifi hotspot`) so band and channel are always set explicitly.

The `status` command shows the active band and channel alongside connected client IPs so you can confirm 2.4 GHz is in use at a glance.

---

### Database Init

```bash
psql -U <user> -d <database> -f init_db.sql
```

Creates two tables:

- **`nodes`** — network topology defining parent-child relationships and voltage ratios
- **`live_status`** — current digital twin state (presence, voltage in/out, status message)

Seeds initial data for a utility node (`util_a`) and transformer node (`trans_a`).
