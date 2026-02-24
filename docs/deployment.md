# Deployment Guide

This guide covers deploying the Winter River IoT system on a Raspberry Pi 5 and
flashing firmware to ESP32 nodes.

## Prerequisites

### Hardware

- **Raspberry Pi 5** with Debian Trixie (arm64), 16 GB+ microSD
- **ESP32-WROOM-32** development boards (1–20 units), USB cables

### Software (development machine)

- PlatformIO Core or PlatformIO IDE
- Python 3.9+
- Git

## Part 1: Raspberry Pi Setup

All Pi-side services are configured by a single idempotent script. Run it once
on a fresh Pi; re-run it any time to apply config changes.

### 1.1 Clone the repository

```bash
cd ~
git clone <repo-url> ECE-26.1-Winter-River
cd ECE-26.1-Winter-River
```

### 1.2 Set credentials

```bash
cp grafana/.env.sample grafana/.env
nano grafana/.env    # replace every changeme_* value
```

Key variables:

| Variable | Description |
|---|---|
| `INFLUXDB_ADMIN_USER` | InfluxDB admin username |
| `INFLUXDB_ADMIN_PASSWORD` | InfluxDB admin password |
| `INFLUXDB_ADMIN_TOKEN` | InfluxDB API token (≥ 32 chars, no spaces) |
| `GF_SECURITY_ADMIN_USER` | Grafana admin username |
| `GF_SECURITY_ADMIN_PASSWORD` | Grafana admin password |

### 1.3 Run setup

```bash
sudo ./scripts/setup_pi.sh
```

The script performs full end-to-end provisioning in this order:

1. Loads credentials from `grafana/.env`
2. Installs system packages: `mosquitto`, `ntpsec`, `curl`, `gnupg`
3. Adds the InfluxData apt repo and installs `influxdb2` + `telegraf`
4. Starts InfluxDB and runs non-interactive initial setup (org `iot-project`, bucket `mqtt_metrics`)
5. Copies `grafana/telegraf.conf` → `/etc/telegraf/telegraf.conf`
6. Writes `/etc/default/telegraf` with the InfluxDB token
7. Starts and enables Telegraf
8. Installs the `grafana-mqtt-datasource` Grafana plugin
9. Copies `grafana/provisioning/` → `/etc/grafana/provisioning/`
10. Copies `grafana/dashboards/` → `/var/lib/grafana/dashboards/`
11. Writes `/etc/default/grafana-server` with admin credentials and the InfluxDB token
12. Starts and enables Grafana
13. Configures Mosquitto with TCP listener on 1883 and WebSocket listener on 9001
14. Sets up the `WinterRiver-AP` WiFi hotspot
15. Configures NTP to serve time to the `192.168.4.0/24` subnet

### 1.4 Verify

```bash
# All services should show "active (running)"
systemctl status influxdb telegraf grafana-server mosquitto

# Quick connectivity test
mosquitto_pub -h 192.168.4.1 -t winter-river/test -m '{"status":"ok"}'
influx ping
```

Access Grafana at `http://192.168.4.1:3000` from any device connected to
`WinterRiver-AP`.

## Part 2: ESP32 Firmware Deployment

### 2.1 Configure firmware

Each node has its own source file under `esp32-nodes/src/<type>/<id>/`.
Edit the WiFi and MQTT settings at the top of the relevant `.cpp` file:

```cpp
const char* WIFI_SSID     = "WinterRiver-AP";
const char* WIFI_PASSWORD = "winterriver";
const char* MQTT_BROKER   = "192.168.4.1";
const int   MQTT_PORT     = 1883;
const char* NODE_ID       = "pdu_a";   // unique per node
```

MQTT publish topic: `winter-river/<NODE_ID>/status`

### 2.2 Build and upload

```bash
# Build a specific node environment (see esp32-nodes/platformio.ini for env names)
cd esp32-nodes
pio run -e pdu_a

# Upload to connected ESP32
pio run -e pdu_a --target upload

# Monitor serial output
pio device monitor -e pdu_a
```

### 2.3 Deploy multiple nodes

Repeat section 2.1–2.2 for each node, updating `NODE_ID` to match the node's
role (`util_a`, `trf_a`, `sw_a`, `gen_a`, `dist_a`, `ups_a`, `pdu_a`, `srv_a`, …).

## Part 3: Network Configuration

The Pi acts as a WiFi access point — ESP32 nodes connect directly to it.

| Parameter | Value |
|---|---|
| SSID | `WinterRiver-AP` |
| Password | `winterriver` |
| Pi gateway / MQTT broker | `192.168.4.1` |
| Band | 2.4 GHz (802.11 b/g) — required for ESP32 |
| Channel | 6 |

### Firewall (if ufw is enabled)

```bash
sudo ufw allow 22/tcp     # SSH
sudo ufw allow 1883/tcp   # MQTT (TCP)
sudo ufw allow 9001/tcp   # MQTT (WebSocket — Grafana Live)
sudo ufw allow 3000/tcp   # Grafana
sudo ufw allow 8086/tcp   # InfluxDB (optional, local only by default)
sudo ufw enable
```

## Part 4: Testing the System

### 4.1 Test MQTT communication

```bash
# Subscribe to all node status messages
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v

# Publish a test payload from another terminal
mosquitto_pub -h 192.168.4.1 -t "winter-river/test/status" \
  -m '{"node_id":"test","voltage":12.0,"status":"ok"}'
```

### 4.2 Verify InfluxDB data

```bash
# Check Telegraf ingestion (wait ~10 s after publishing)
influx query '
  from(bucket: "mqtt_metrics")
    |> range(start: -5m)
    |> limit(n: 10)
' --org iot-project --token "$INFLUXDB_ADMIN_TOKEN"
```

Or open the InfluxDB UI at `http://192.168.4.1:8086` and use the Data Explorer.

### 4.3 Verify Grafana dashboards

1. Open `http://192.168.4.1:3000`
2. Log in with the credentials from `grafana/.env`
3. Confirm that the Broker Overview and Nodes Telemetry dashboards load
4. Confirm that data appears within ~10 s of MQTT messages being published

## Part 5: Service Management

```bash
# Status
systemctl status influxdb telegraf grafana-server mosquitto

# Restart after config changes
sudo systemctl restart telegraf
sudo systemctl restart grafana-server
sudo systemctl restart mosquitto

# Logs (live)
journalctl -u influxdb       -f
journalctl -u telegraf       -f
journalctl -u grafana-server -f
journalctl -u mosquitto      -f
```

## Part 6: Production Checklist

- [ ] Set real passwords in `grafana/.env` and re-run `setup_pi.sh`
- [ ] Rotate the InfluxDB token after initial setup
- [ ] Enable MQTT authentication (username/password) in mosquitto if the Pi
      will be reachable from untrusted networks
- [ ] Configure log rotation for `/var/log/mosquitto/`
- [ ] Set up monitoring/alerting rules in Grafana
- [ ] Document node IDs and physical locations
- [ ] Disable SSH password auth (use keys only)
- [ ] Enable automatic security updates (`unattended-upgrades`)

## Backup Procedures

```bash
# InfluxDB — export all data
influx backup /tmp/influxdb-backup-$(date +%Y%m%d) \
  --org iot-project --token "$INFLUXDB_ADMIN_TOKEN"

# Grafana dashboards — copy JSON files
cp -r /var/lib/grafana/dashboards ~/grafana-dashboards-backup-$(date +%Y%m%d)

# Mosquitto config
cp /etc/mosquitto/mosquitto.conf ~/mosquitto.conf.$(date +%Y%m%d)
```

## Troubleshooting

### Mosquitto not starting

```bash
sudo journalctl -u mosquitto -n 50
mosquitto -c /etc/mosquitto/mosquitto.conf -v   # config test
sudo ss -tlnp | grep -E '1883|9001'             # port check
```

### Telegraf not writing to InfluxDB

```bash
sudo journalctl -u telegraf -n 50
# Confirm INFLUX_TOKEN is set in the service environment:
sudo systemctl show telegraf --property=Environment
```

### Grafana shows no data

```bash
sudo journalctl -u grafana-server -n 50
# Confirm INFLUXDB_TOKEN is set:
sudo systemctl show grafana-server --property=Environment
# Re-run setup to refresh credentials:
sudo ./scripts/setup_pi.sh
```

### ESP32 cannot connect to WiFi

- Confirm SSID/password match `WinterRiver-AP` / `winterriver`
- ESP32 only supports 2.4 GHz — check the hotspot is broadcasting on 2.4 GHz
- Check serial monitor: `pio device monitor -e <env>`

### ESP32 cannot connect to MQTT broker

```bash
# From the Pi:
ping 192.168.4.1          # should always succeed (self)
systemctl status mosquitto
```

- Confirm the node's `MQTT_BROKER` is set to `192.168.4.1`
- Test with `mosquitto_sub` while the node is running

## References

- [Raspberry Pi 5 Documentation](https://www.raspberrypi.com/documentation/)
- [InfluxDB 2 CLI Reference](https://docs.influxdata.com/influxdb/v2/reference/cli/influx/)
- [Telegraf MQTT Consumer Plugin](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/mqtt_consumer)
- [Mosquitto Configuration](https://mosquitto.org/man/mosquitto-conf-5.html)
- [PlatformIO CLI Reference](https://docs.platformio.org/en/latest/core/index.html)
- [Grafana Provisioning](https://grafana.com/docs/grafana/latest/administration/provisioning/)
