# Grafana Visualization Setup

This directory contains Grafana configuration for visualizing MQTT broker metrics
and ESP32 node telemetry.  The stack runs **natively on the Raspberry Pi** via
systemd — no Docker required.

## Architecture

```
grafana/
├── README.md                          # This file
├── docker-compose.yml                 # DEPRECATED — kept for reference only
├── grafana.ini                        # Grafana tunables (referenced in docs)
├── telegraf.conf                      # Telegraf MQTT→InfluxDB bridge config
├── .env.sample                        # Credential template (copy → .env)
├── dashboards/                        # Dashboard JSON definitions
│   ├── broker-overview.json          # MQTT broker metrics dashboard
│   └── nodes-telemetry.json          # ESP32 nodes telemetry dashboard
└── provisioning/                      # Grafana auto-provisioning configs
    ├── dashboards/
    │   └── dashboard.yml             # Dashboard provisioning config
    └── datasources/
        └── datasource.yml            # Data source provisioning config
```

## Data flow

```
ESP32 nodes
    │  MQTT  (TCP 1883)
    ▼
Mosquitto broker  (192.168.4.1)
    │  MQTT  (TCP 1883)          ─── also WebSocket (WS 9001) ──► Grafana MQTT Live
    ▼
Telegraf  (MQTT consumer)
    │  Line Protocol
    ▼
InfluxDB 2  (localhost:8086, bucket: mqtt_metrics)
    │  Flux queries
    ▼
Grafana  (http://192.168.4.1:3000)
```

## Prerequisites

- Raspberry Pi 5 running Debian Trixie (arm64)
- Grafana installed natively (e.g. via the official Grafana apt repo)
- Project cloned to `~/ECE-26.1-Winter-River`

## Setup

### 1. Configure credentials

```bash
cp grafana/.env.sample grafana/.env
nano grafana/.env          # Set real passwords and token
```

Key variables in `.env`:

| Variable | Description |
|---|---|
| `INFLUXDB_ADMIN_USER` | InfluxDB admin username |
| `INFLUXDB_ADMIN_PASSWORD` | InfluxDB admin password |
| `INFLUXDB_ADMIN_TOKEN` | InfluxDB API token (≥ 32 chars) |
| `GF_SECURITY_ADMIN_USER` | Grafana admin username |
| `GF_SECURITY_ADMIN_PASSWORD` | Grafana admin password |

### 2. Run setup

```bash
sudo ./scripts/setup_pi.sh
```

`setup_pi.sh` is **idempotent** — safe to re-run after changing `.env`.

It will:
1. Add the InfluxData apt repo and install `influxdb2` + `telegraf`
2. Run InfluxDB non-interactive initial setup (org `iot-project`, bucket `mqtt_metrics`)
3. Write credentials into `/etc/default/telegraf` and `/etc/default/grafana-server`
4. Copy `grafana/telegraf.conf` → `/etc/telegraf/telegraf.conf`
5. Copy `grafana/provisioning/` → `/etc/grafana/provisioning/`
6. Copy `grafana/dashboards/` → `/var/lib/grafana/dashboards/`
7. Install the `grafana-mqtt-datasource` Grafana plugin
8. Enable and start `influxdb`, `telegraf`, and `grafana-server` via systemd
9. Add Mosquitto WebSocket listener on port 9001

### 3. Access Grafana

Open `http://192.168.4.1:3000` in a browser connected to the **WinterRiver-AP**
hotspot.  Login with the credentials you set in `.env`.

## Service management

```bash
# Status
systemctl status influxdb telegraf grafana-server

# Restart after config changes
sudo systemctl restart telegraf
sudo systemctl restart grafana-server

# Logs
journalctl -u influxdb      -f
journalctl -u telegraf      -f
journalctl -u grafana-server -f
```

## Datasources

Two datasources are provisioned automatically:

| Name | Type | URL | Purpose |
|---|---|---|---|
| `InfluxDB-MQTT` | InfluxDB v2 (Flux) | `http://localhost:8086` | Historical time-series |
| `MQTT-Live` | grafana-mqtt-datasource | `ws://192.168.4.1:9001` | Real-time panel streaming |

The InfluxDB token is injected at startup via `GF_*` environment variables in
`/etc/default/grafana-server` — no plain-text tokens in the repo.

## Dashboards

Dashboards are automatically provisioned from `/var/lib/grafana/dashboards/`.

**Broker Overview** — displays:
- Active connections
- Message throughput (messages/sec)
- Published/received bytes
- Topic subscription counts

**Nodes Telemetry** — displays per ESP32 node:
- Connection status
- Sensor readings (voltage, current, etc.)
- WiFi signal strength (RSSI)
- Uptime and message latency

## Scaling to more nodes

1. Edit `dashboards/nodes-telemetry.json`
2. Duplicate existing node panels and update Flux queries to filter by `node_id`
3. Use Grafana's *repeat panel* feature with a `node_id` dashboard variable for
   automatic scaling

## Plugins

The following plugin is installed automatically by `setup_pi.sh`:

- **grafana-mqtt-datasource** — real-time MQTT streaming panels

To install manually:

```bash
sudo grafana-cli plugins install grafana-mqtt-datasource
sudo systemctl restart grafana-server
```

## MQTT topic schema

```
winter-river/<node_id>/status
```

Example payload:

```json
{"node_id": "pdu_a", "voltage": 12.1, "current": 2.3, "status": "ok"}
```

Telegraf subscribes to `winter-river/#` and writes all numeric fields to the
`mqtt_metrics` InfluxDB bucket, tagging by `node_id` and `status`.

## Environment variables reference

| Variable (in `/etc/default/telegraf`) | Purpose |
|---|---|
| `INFLUX_TOKEN` | Telegraf → InfluxDB write token |

| Variable (in `/etc/default/grafana-server`) | Purpose |
|---|---|
| `GF_SECURITY_ADMIN_USER` | Grafana admin username |
| `GF_SECURITY_ADMIN_PASSWORD` | Grafana admin password |
| `INFLUXDB_TOKEN` | Grafana datasource token (expanded in `datasource.yml`) |
| `GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH` | Home dashboard path |
