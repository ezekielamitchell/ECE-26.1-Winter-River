# Grafana Visualization Setup

This directory contains Grafana configuration for visualizing MQTT broker metrics and ESP32 node telemetry.

## Overview

- **Broker Monitoring**: Visualize mosquitto broker statistics and connection metrics
- **Node Telemetry**: Dashboard for monitoring up to 3 ESP32 nodes (scalable to 20)
- **Real-time Updates**: Live visualization of sensor data and system health

## Architecture

```
grafana/
├── README.md                          # This file
├── docker-compose.yml                 # Grafana + data source containers
├── grafana.ini                        # Grafana configuration
├── dashboards/                        # Dashboard JSON definitions
│   ├── broker-overview.json          # MQTT broker metrics dashboard
│   └── nodes-telemetry.json          # ESP32 nodes telemetry dashboard
└── provisioning/                      # Auto-provisioning configs
    ├── dashboards/
    │   └── dashboard.yml             # Dashboard provisioning config
    └── datasources/
        └── datasource.yml            # Data source provisioning config
```

## Prerequisites

- Docker and Docker Compose installed on Raspberry Pi
- MQTT broker (mosquitto) running and accessible
- Time-series database for metrics (InfluxDB recommended)

## Setup Instructions

### 1. Choose a Data Source

This setup assumes you'll use InfluxDB as the time-series database for storing MQTT metrics.

```bash
# Install InfluxDB via Docker (included in docker-compose.yml)
# Or install directly on Raspberry Pi
```

### 2. Configure MQTT to InfluxDB Bridge

You'll need a bridge service to write MQTT messages to InfluxDB:
- Option A: Use Telegraf with MQTT consumer plugin
- Option B: Implement custom Python service using paho-mqtt + influxdb client
- Option C: Use Node-RED for visual flow-based bridging

### 3. Start Grafana Stack

```bash
cd grafana
docker-compose up -d
```

Grafana will be available at `http://<raspberry-pi-ip>:3000`

Default credentials:
- Username: `admin`
- Password: `admin` (change on first login)

### 4. Dashboard Configuration

Dashboards are automatically provisioned from the `dashboards/` directory.

**Broker Overview Dashboard** displays:
- Active connections
- Message throughput (messages/sec)
- Published/received bytes
- Topic subscription counts
- Client session info

**Nodes Telemetry Dashboard** displays (per node):
- Connection status
- Sensor readings (customize for your sensors)
- WiFi signal strength (RSSI)
- Uptime
- Message latency
- Error counts

## Scaling to More Nodes

The node dashboard is designed to handle 3 nodes initially but can scale to 20+:

1. Edit `dashboards/nodes-telemetry.json`
2. Add new panels by duplicating existing node panels
3. Update panel queries to filter by new node IDs
4. Use dashboard variables for dynamic node selection

Or use Grafana's repeat panel feature:
- Create a variable for node IDs
- Configure panels to repeat for each node

## Development Tasks

- [ ] Set up InfluxDB container
- [ ] Configure Telegraf MQTT consumer
- [ ] Create broker metrics collection script
- [ ] Design broker overview dashboard
- [ ] Design node telemetry dashboard with 3 node panels
- [ ] Test with live MQTT data
- [ ] Add alerting rules (optional)
- [ ] Configure dashboard auto-refresh
- [ ] Document custom queries

## Grafana Plugins

Consider installing these plugins for enhanced functionality:
- MQTT data source plugin (if available)
- Worldmap panel (for node location visualization)
- Flowchart plugin (for system architecture visualization)

## Environment Variables

Create a `.env` file in this directory (git-ignored) with:

```env
# InfluxDB Configuration
INFLUXDB_DB=mqtt_metrics
INFLUXDB_ADMIN_USER=admin
INFLUXDB_ADMIN_PASSWORD=changeme
INFLUXDB_USER=grafana
INFLUXDB_USER_PASSWORD=changeme

# Grafana Configuration
GF_SECURITY_ADMIN_USER=admin
GF_SECURITY_ADMIN_PASSWORD=changeme
GF_INSTALL_PLUGINS=

# MQTT Broker
MQTT_BROKER_HOST=localhost
MQTT_BROKER_PORT=1883
```

## Resources

- [Grafana Documentation](https://grafana.com/docs/)
- [InfluxDB Documentation](https://docs.influxdata.com/)
- [Telegraf MQTT Consumer Plugin](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/mqtt_consumer)
