# Winter River Grafana Setup Guide

This guide walks you through setting up Grafana for the Winter River tabletop
data center simulator. It is written for a fresh Raspberry Pi install and keeps
the steps simple enough to follow during a capstone demo prep session.

The current Winter River monitoring stack runs as native Raspberry Pi services:

- Mosquitto receives MQTT telemetry from ESP32 nodes.
- Telegraf subscribes to MQTT and writes JSON telemetry into InfluxDB.
- InfluxDB stores the time-series data in the `mqtt_metrics` bucket.
- Grafana reads InfluxDB and displays the dashboards.
- Grafana can also connect to Mosquitto over WebSocket for live MQTT panels.

The old `docker-compose.yml` file is kept for reference only. Use the native
systemd setup in this guide for the Raspberry Pi deployment.

## End Result

After setup, you should have these services running on the Raspberry Pi:

| Service | Purpose | Address |
| ------- | ------- | ------- |
| Mosquitto | MQTT broker for ESP32 nodes | `192.168.4.1:1883` |
| Mosquitto WebSocket | Live Grafana MQTT datasource | `ws://192.168.4.1:9001` |
| InfluxDB 2 | Time-series database | `http://localhost:8086` |
| Telegraf | MQTT to InfluxDB bridge | local service |
| Grafana | Dashboard UI | `http://192.168.4.1:3000` |

## Data Flow

```text
ESP32 nodes
  publish JSON telemetry
      |
      v
Mosquitto MQTT broker
  topic: winter-river/<node_id>/status
      |
      v
Telegraf mqtt_consumer
  parses JSON and extracts node_id from the topic
      |
      v
InfluxDB 2
  org: iot-project
  bucket: mqtt_metrics
      |
      v
Grafana
  datasource: InfluxDB-MQTT
  dashboards: broker-overview.json, nodes-telemetry.json
```

## Before You Start

You need:

- Raspberry Pi 5 running Raspberry Pi OS or Debian.
- Internet access on the Pi during setup for apt packages.
- This repository cloned on the Pi.
- A terminal on the Pi, either local keyboard/monitor or SSH.

Important path requirement:

`scripts/setup_pi.sh` expects the repo to be here:

```bash
/home/<your-pi-username>/ECE-26.1-Winter-River
```

For the default `pi` user, that means:

```bash
/home/pi/ECE-26.1-Winter-River
```

If your repo is somewhere else, move it before running setup:

```bash
cd /home/pi
git clone <repo-url> ECE-26.1-Winter-River
cd ECE-26.1-Winter-River
```

Replace `<repo-url>` with the GitHub URL for this project.

## Quick Start

Run these commands on the Raspberry Pi:

```bash
cd ~/ECE-26.1-Winter-River

cp grafana/.env.sample grafana/.env
nano grafana/.env

sudo ./scripts/setup_pi.sh

./scripts/status.sh
```

Then connect your laptop to the Pi hotspot:

```text
SSID: WinterRiver-AP
Password: winterriver
```

Open Grafana:

```text
http://192.168.4.1:3000
```

Log in with the Grafana username and password you put in `grafana/.env`.

## Step 1: Create Grafana and InfluxDB Credentials

From the project root:

```bash
cd ~/ECE-26.1-Winter-River
cp grafana/.env.sample grafana/.env
nano grafana/.env
```

Edit every placeholder value.

Example:

```bash
INFLUXDB_ADMIN_USER=influx-admin
INFLUXDB_ADMIN_PASSWORD=replace_with_a_strong_influx_password
INFLUXDB_ADMIN_TOKEN=replace_with_a_long_random_token_at_least_32_chars

GF_SECURITY_ADMIN_USER=admin
GF_SECURITY_ADMIN_PASSWORD=replace_with_a_strong_grafana_password
```

Rules:

- `INFLUXDB_ADMIN_TOKEN` must be at least 32 characters.
- Do not use spaces in the token.
- Do not commit `grafana/.env`.
- If you change this file later, rerun `sudo ./scripts/setup_pi.sh`.

## Step 2: Run the Raspberry Pi Setup Script

From the project root:

```bash
sudo ./scripts/setup_pi.sh
```

This script does the Grafana setup and the supporting system setup. It is safe
to rerun.

It will:

1. Install Mosquitto, InfluxDB 2, Telegraf, Grafana, PostgreSQL, Python tools,
   and required package repositories.
2. Configure the Raspberry Pi hotspot as `WinterRiver-AP`.
3. Configure Mosquitto on TCP port `1883` for ESP32 nodes.
4. Configure Mosquitto WebSocket port `9001` for Grafana live MQTT panels.
5. Create the InfluxDB org `iot-project`.
6. Create the InfluxDB bucket `mqtt_metrics`.
7. Copy `grafana/telegraf.conf` to `/etc/telegraf/telegraf.conf`.
8. Store the InfluxDB token in `/etc/default/telegraf`.
9. Copy Grafana provisioning files into `/etc/grafana/provisioning/`.
10. Copy dashboard JSON files into `/var/lib/grafana/dashboards/`.
11. Install the Grafana MQTT datasource plugin.
12. Store Grafana credentials and the InfluxDB token in
    `/etc/default/grafana-server`.
13. Enable services so they restart automatically after boot.

At the end, the script prints the service status and the main URLs.

## Step 3: Check Services

Run:

```bash
./scripts/status.sh
```

You want to see these services running:

```text
winter-river-hotspot
mosquitto
influxdb
telegraf
postgresql
grafana-server
ntpsec
```

You can also check only the monitoring services:

```bash
systemctl status influxdb telegraf grafana-server
```

If a service is not running, inspect logs:

```bash
sudo journalctl -u influxdb -n 50 --no-pager
sudo journalctl -u telegraf -n 50 --no-pager
sudo journalctl -u grafana-server -n 50 --no-pager
sudo journalctl -u mosquitto -n 50 --no-pager
```

## Step 4: Open Grafana

Connect your computer to the Pi hotspot:

```text
SSID: WinterRiver-AP
Password: winterriver
```

Then open:

```text
http://192.168.4.1:3000
```

Log in with:

- Username: `GF_SECURITY_ADMIN_USER` from `grafana/.env`
- Password: `GF_SECURITY_ADMIN_PASSWORD` from `grafana/.env`

The default home dashboard is configured in `grafana/grafana.ini`:

```text
/var/lib/grafana/dashboards/broker-overview.json
```

## Step 5: Confirm Grafana Datasources

In Grafana:

1. Go to Connections.
2. Open Data sources.
3. Confirm these datasources exist:

| Name | Type | Used For |
| ---- | ---- | -------- |
| `InfluxDB-MQTT` | InfluxDB | Historical telemetry from Telegraf |
| `MQTT-Live` | MQTT | Live MQTT panels over WebSocket |

The `InfluxDB-MQTT` datasource should point to:

```text
http://localhost:8086
```

The `MQTT-Live` datasource should point to:

```text
ws://192.168.4.1:9001
```

## Step 6: Confirm MQTT Telemetry Is Arriving

If ESP32 nodes are powered on, watch live MQTT traffic:

```bash
./scripts/status.sh mqtt
```

You should see messages like:

```text
winter-river/ups_a/status {"ts":"14:32:01","battery_pct":100,"load_pct":40,"input_v":480.0,"output_v":480.0,"state":"NORMAL","voltage":480}
```

If you do not have ESP32 nodes powered yet, publish a test message from the Pi:

```bash
mosquitto_pub -h 127.0.0.1 \
  -t "winter-river/test_node/status" \
  -m '{"ts":"12:00:00","voltage":480,"load_pct":42,"state":"NORMAL"}'
```

Then subscribe once to confirm the broker retained or delivered data:

```bash
mosquitto_sub -h 127.0.0.1 -t "winter-river/+/status" -C 1 -v
```

## Step 7: Confirm Telegraf Is Writing to InfluxDB

Check Telegraf logs:

```bash
sudo journalctl -u telegraf -n 80 --no-pager
```

If Telegraf is healthy, you should not see repeated JSON parse errors or
InfluxDB authorization errors.

You can also test the Telegraf config directly:

```bash
sudo telegraf --config /etc/telegraf/telegraf.conf --test
```

## Step 8: Query InfluxDB Manually

Use the token from `grafana/.env`:

```bash
source grafana/.env
influx query \
  --org iot-project \
  --token "$INFLUXDB_ADMIN_TOKEN" \
  'from(bucket: "mqtt_metrics")
    |> range(start: -15m)
    |> limit(n: 10)'
```

If data is flowing, this returns recent telemetry rows.

No rows usually means one of these is true:

- No ESP32 node has published telemetry yet.
- Mosquitto is not receiving messages.
- Telegraf is stopped.
- Telegraf cannot authenticate to InfluxDB.
- The data is older than the query range.

## Step 9: Open the Dashboards

Grafana automatically loads dashboard JSON files from:

```text
/var/lib/grafana/dashboards
```

The repo versions live here:

```text
grafana/dashboards/
```

Current dashboards:

| Dashboard | File | Purpose |
| --------- | ---- | ------- |
| MQTT Broker Overview | `broker-overview.json` | Broker and MQTT health view |
| Winter River - Node Status | `nodes-telemetry.json` | ESP32 node telemetry and state |

If you edit dashboard JSON in the repo, rerun setup to copy it to Grafana:

```bash
sudo ./scripts/setup_pi.sh
```

Or copy dashboards manually:

```bash
sudo cp grafana/dashboards/*.json /var/lib/grafana/dashboards/
sudo chown -R grafana:grafana /var/lib/grafana/dashboards
sudo systemctl restart grafana-server
```

## Common Operations

Restart the full monitoring stack:

```bash
sudo systemctl restart influxdb
sudo systemctl restart telegraf
sudo systemctl restart grafana-server
```

Restart only Telegraf after changing `grafana/telegraf.conf`:

```bash
sudo cp grafana/telegraf.conf /etc/telegraf/telegraf.conf
sudo systemctl restart telegraf
```

Restart only Grafana after changing dashboards or provisioning:

```bash
sudo systemctl restart grafana-server
```

Watch Grafana logs live:

```bash
sudo journalctl -u grafana-server -f
```

Watch Telegraf logs live:

```bash
sudo journalctl -u telegraf -f
```

Watch MQTT messages live:

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
```

## Important Files

| File | What It Does |
| ---- | ------------ |
| `grafana/.env.sample` | Template for local credentials |
| `grafana/.env` | Your real local credentials, git ignored |
| `grafana/telegraf.conf` | Telegraf MQTT to InfluxDB bridge |
| `grafana/grafana.ini` | Grafana server settings |
| `grafana/provisioning/datasources/datasource.yml` | Auto-creates InfluxDB and MQTT datasources |
| `grafana/provisioning/dashboards/dashboard.yml` | Auto-loads dashboard JSON files |
| `grafana/dashboards/*.json` | Dashboard definitions |
| `scripts/setup_pi.sh` | Full Raspberry Pi setup script |
| `deploy/mosquitto_setup.sh` | Mosquitto TCP and WebSocket setup |

## MQTT Topic Format

Telegraf subscribes to:

```text
winter-river/+/status
```

That matches node telemetry topics such as:

```text
winter-river/utility_a/status
winter-river/ups_a/status
winter-river/cooling_b/status
```

Telegraf extracts the middle path segment as the `node_id` tag.

For example:

```text
winter-river/ups_a/status
```

becomes:

```text
node_id=ups_a
```

Payloads must be JSON. Numeric values become queryable fields in InfluxDB.
String values such as `state`, `status`, `source`, and `ts` are also preserved.

Example payload:

```json
{"ts":"14:32:01","battery_pct":100,"load_pct":40,"input_v":480.0,"output_v":480.0,"state":"NORMAL","voltage":480}
```

## Troubleshooting

### Grafana page does not load

Check that Grafana is running:

```bash
systemctl status grafana-server
```

Check that the Pi is listening on port `3000`:

```bash
ss -ltnp | grep 3000
```

Make sure your laptop is connected to `WinterRiver-AP`, then open:

```text
http://192.168.4.1:3000
```

### Cannot log in to Grafana

Check the credentials in:

```bash
cat grafana/.env
```

Then rerun setup so the service environment is rewritten:

```bash
sudo ./scripts/setup_pi.sh
```

If needed, reset the admin password directly:

```bash
sudo grafana-cli admin reset-admin-password '<new-password>'
sudo systemctl restart grafana-server
```

### Dashboards are missing

Check whether dashboard JSON files were copied:

```bash
ls -l /var/lib/grafana/dashboards
```

Copy and restart:

```bash
sudo cp grafana/dashboards/*.json /var/lib/grafana/dashboards/
sudo chown -R grafana:grafana /var/lib/grafana/dashboards
sudo systemctl restart grafana-server
```

### InfluxDB datasource fails in Grafana

Check that InfluxDB is running:

```bash
curl http://localhost:8086/health
```

Check that Grafana received the InfluxDB token:

```bash
sudo grep INFLUXDB_TOKEN /etc/default/grafana-server
```

Restart Grafana:

```bash
sudo systemctl restart grafana-server
```

### Telegraf shows authorization errors

The token in `/etc/default/telegraf` may not match InfluxDB.

Rewrite service configuration from `grafana/.env`:

```bash
sudo ./scripts/setup_pi.sh
```

Then check logs:

```bash
sudo journalctl -u telegraf -n 80 --no-pager
```

### Telegraf shows JSON parse errors

Make sure MQTT payloads are valid JSON.

Good:

```json
{"voltage":480,"load_pct":42,"state":"NORMAL"}
```

Bad:

```text
voltage=480 load_pct=42 state=NORMAL
```

### MQTT works, but Grafana has no data

Check each link in the chain:

```bash
systemctl status mosquitto
systemctl status telegraf
systemctl status influxdb
systemctl status grafana-server
```

Publish a test telemetry message:

```bash
mosquitto_pub -h 127.0.0.1 \
  -t "winter-river/debug_node/status" \
  -m '{"voltage":480,"load_pct":42,"state":"NORMAL"}'
```

Wait 10 seconds, then query InfluxDB:

```bash
source grafana/.env
influx query \
  --org iot-project \
  --token "$INFLUXDB_ADMIN_TOKEN" \
  'from(bucket: "mqtt_metrics")
    |> range(start: -5m)
    |> filter(fn: (r) => r["node_id"] == "debug_node")
    |> limit(n: 10)'
```

If this returns rows, Grafana should be able to show the data. Refresh the
dashboard time range to the last 5 or 15 minutes.

## Clean Redeploy

For normal changes, rerun:

```bash
sudo ./scripts/setup_pi.sh
```

For a deeper service restart:

```bash
sudo systemctl restart mosquitto influxdb telegraf grafana-server
```

Avoid deleting InfluxDB data unless you intentionally want to remove historical
telemetry.

## Security Notes

This capstone stack is designed for a closed classroom/demo network.

Current defaults:

- Grafana login is required.
- Mosquitto allows anonymous MQTT clients.
- The WiFi hotspot uses WPA-PSK.
- The example password is suitable for demos, not production.

Before any public or long-running deployment:

- Change the hotspot password in `scripts/setup_hotspot.sh`.
- Use strong unique Grafana and InfluxDB credentials.
- Consider disabling anonymous Mosquitto access.
- Do not expose Grafana, InfluxDB, or MQTT directly to the public internet.

## Setup Checklist

Use this checklist during demo prep:

- Repo is cloned to `/home/<user>/ECE-26.1-Winter-River`.
- `grafana/.env` exists and has real credentials.
- `sudo ./scripts/setup_pi.sh` completed without fatal errors.
- `./scripts/status.sh` shows Grafana, InfluxDB, Telegraf, and Mosquitto running.
- Laptop is connected to `WinterRiver-AP`.
- Grafana opens at `http://192.168.4.1:3000`.
- `InfluxDB-MQTT` datasource exists.
- `MQTT-Live` datasource exists.
- ESP32 telemetry appears with `./scripts/status.sh mqtt`.
- Recent data appears in the Winter River dashboards.
