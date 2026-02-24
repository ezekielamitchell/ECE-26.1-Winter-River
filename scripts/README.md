# Scripts

Utility scripts for setting up and operating the Winter River IoT system on
Raspberry Pi 5.

## Contents

| File | Description |
|------|-------------|
| `setup_pi.sh` | Full Pi bootstrap — installs and configures every native service |
| `setup_hotspot.sh` | Creates the `WinterRiver-AP` 2.4 GHz WiFi access point |
| `status.sh` | Live node and service health check |
| `init_db.sql` | Database schema for the digital twin (node topology and live status) |

---

## Usage

### Full Pi Setup

Run once on a fresh Raspberry Pi after cloning the repo. Re-running is safe —
all steps are idempotent.

```bash
# Set credentials first (copy the sample and fill in real values)
cp grafana/.env.sample grafana/.env
nano grafana/.env

sudo ./scripts/setup_pi.sh
```

Performs end-to-end provisioning in this order:

1. Loads credentials from `grafana/.env` (falls back to placeholder defaults with a warning)
2. Installs system packages: `mosquitto`, `mosquitto-clients`, `ntpsec`, `curl`, `gnupg`
3. Adds the InfluxData apt repo (arm64) and installs `influxdb2` + `telegraf`
4. Starts InfluxDB and runs non-interactive initial setup (org `iot-project`, bucket `mqtt_metrics`)
5. Copies `grafana/telegraf.conf` → `/etc/telegraf/telegraf.conf`
6. Writes `/etc/default/telegraf` with the InfluxDB token and restarts Telegraf
7. Installs the `grafana-mqtt-datasource` Grafana plugin
8. Copies `grafana/provisioning/` → `/etc/grafana/provisioning/`
9. Copies `grafana/dashboards/` → `/var/lib/grafana/dashboards/`
10. Writes `/etc/default/grafana-server` with admin credentials and token, restarts Grafana
11. Calls `deploy/mosquitto_setup.sh` — configures Mosquitto with TCP `:1883` and WebSocket `:9001`
12. Calls `setup_hotspot.sh start` — brings up the `WinterRiver-AP` WiFi AP
13. Configures NTP to serve time to the `192.168.4.0/24` subnet (used by ESP32 nodes)
14. Enables all services for auto-boot via systemd

Services enabled at the end: `influxdb`, `telegraf`, `grafana-server`,
`mosquitto`, `winter-river-hotspot`, `ntpsec`.

---

### WiFi Hotspot

```bash
sudo ./scripts/setup_hotspot.sh          # Start hotspot
sudo ./scripts/setup_hotspot.sh stop     # Stop hotspot
sudo ./scripts/setup_hotspot.sh status   # Show status and connected clients
```

Creates an access point on `wlan0`:

| Setting | Value |
|---------|-------|
| SSID | `WinterRiver-AP` |
| Password | `winterriver` |
| Gateway / MQTT broker | `192.168.4.1` |
| Band | **2.4 GHz (802.11 b/g)** |
| Channel | **6** |
| DHCP range | `192.168.4.x` |

> **Why 2.4 GHz is forced:** The ESP32-WROOM-32 only supports 2.4 GHz
> (802.11 b/g/n). Without explicitly setting `wifi.band bg` and
> `wifi.channel 6`, NetworkManager may default to 5 GHz and nodes will
> fail to connect.

---

### Database Init

```bash
psql -U <user> -d <database> -f scripts/init_db.sql
```

Creates two tables:

- **`nodes`** — network topology defining parent-child relationships and voltage ratios
- **`live_status`** — current digital twin state (presence, voltage in/out, status message)

Seeds initial data for `util_a` (utility node) and `trans_a` (transformer node).
