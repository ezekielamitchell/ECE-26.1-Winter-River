# System Architecture

## Overview

The Winter River system consists of three main layers:

1. **ESP32 Nodes** — edge devices that simulate data center components
2. **Raspberry Pi 5** — central controller: MQTT broker, simulation engine, and monitoring stack
3. **Visualization Layer** — Grafana dashboards backed by InfluxDB, fed by Telegraf

## Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Raspberry Pi 5                               │
│                                                                      │
│  ┌───────────────────┐   ┌────────────┐   ┌──────────────────────┐  │
│  │    mosquitto      │   │  Python    │   │  Grafana  :3000      │  │
│  │  MQTT Broker      │◄──│  Broker    │   │  InfluxDB :8086      │  │
│  │  TCP  :1883       │   │  Utilities │   │  Telegraf (daemon)   │  │
│  │  WS   :9001  ─────┼───┼────────────┼───►                      │  │
│  └────────┬──────────┘   └────────────┘   └──────────────────────┘  │
│           │                                        ▲                │
└───────────┼────────────────────────────────────────┼────────────────┘
            │ MQTT (TCP 1883)                         │ MQTT → InfluxDB
            │ pub/sub                                 │ (Telegraf)
            │
    ┌───────┴────────────────────────────────────┐
    │                                            │
    ▼                    ▼                       ▼
┌──────────┐      ┌──────────┐           ┌──────────┐
│  ESP32   │      │  ESP32   │    ...    │  ESP32   │
│  pdu_a   │      │  ups_a   │           │  srv_a   │
│          │      │          │           │          │
│ - OLED   │      │ - OLED   │           │ - OLED   │
│ - WiFi   │      │ - WiFi   │           │ - WiFi   │
│ - MQTT   │      │ - MQTT   │           │ - MQTT   │
└──────────┘      └──────────┘           └──────────┘
```

## Component Details

### Mosquitto MQTT Broker

- **Role**: Central message broker for pub/sub messaging
- **Listeners**:
  - TCP `:1883` — ESP32 nodes, Telegraf, Python utilities
  - WebSocket `:9001` — Grafana MQTT Live datasource plugin
- **Config managed by**: `deploy/mosquitto_setup.sh`

### Python Broker Utilities

- **Role**: Simulation engine and broker management tools
- **Library**: paho-mqtt
- **Responsibilities**:
  - Subscribe to node status topics
  - Calculate and publish system-wide power flow and failure propagation
  - Publish commands back to ESP32 nodes

### ESP32 Nodes

- **Role**: Edge devices simulating data center components
- **Platform**: PlatformIO + Arduino framework (ESP32-WROOM-32)
- **Responsibilities**:
  - Connect to the `WinterRiver-AP` hotspot (2.4 GHz)
  - Establish MQTT connection to `192.168.4.1:1883`
  - Publish JSON status payloads on `winter-river/<node_id>/status`
  - Subscribe to command topics and execute received commands
  - Display real-time state on integrated OLED

### Grafana Visualization Stack

All three services run as native systemd units on the Raspberry Pi — no Docker.

| Service | Unit | Port | Role |
|---|---|---|---|
| InfluxDB 2 | `influxdb` | 8086 | Time-series metrics storage |
| Telegraf | `telegraf` | — | MQTT consumer → InfluxDB bridge |
| Grafana | `grafana-server` | 3000 | Dashboard and alerting UI |

Credentials and tokens are injected at setup time via `/etc/default/telegraf` and
`/etc/default/grafana-server`, written by `scripts/setup_pi.sh`.

## Communication Flow

### Telemetry path (ESP32 → Broker → Grafana)

1. ESP32 reads sensor/simulation data
2. ESP32 publishes JSON payload to `winter-river/<node_id>/status`
3. Mosquitto distributes the message to all subscribers
4. Telegraf (subscribed to `winter-river/#`) receives the message
5. Telegraf parses JSON and writes fields to InfluxDB (`mqtt_metrics` bucket)
6. Grafana queries InfluxDB with Flux and renders the dashboard

### Live streaming path (Broker → Grafana MQTT Live)

1. Mosquitto exposes a WebSocket listener on port 9001
2. Grafana's `grafana-mqtt-datasource` plugin connects to `ws://192.168.4.1:9001`
3. Panels subscribed to `winter-river/#` update in real time without polling InfluxDB

### Command path (Python → Broker → ESP32)

1. Python utility publishes a command to `winter-river/<node_id>/command`
2. Mosquitto routes the message to the subscribed ESP32
3. ESP32 executes the command and publishes an updated status payload

## Topic Structure

```
winter-river/
├── <node_id>/
│   ├── status     # Node telemetry + health (JSON) — published by ESP32
│   └── command    # Commands to the node (JSON)   — published by broker/Pi
│
│   Node IDs: util_a, trf_a, sw_a, gen_a, dist_a, ups_a, pdu_a, srv_a, ...
```

## Data Format

### Status payload (ESP32 → Broker)

```json
{
  "node_id": "pdu_a",
  "voltage_in": 480.0,
  "current": 12.5,
  "power_kw": 6.0,
  "status": "ok",
  "wifi_rssi": -62,
  "uptime": 3600
}
```

Telegraf tags `node_id` and `status`; all numeric keys become InfluxDB fields.

### Command payload (Broker → ESP32)

```json
{
  "command": "set_fault",
  "params": { "state": true }
}
```

## Network Configuration

| Interface | Address | Purpose |
|---|---|---|
| `wlan0` (AP mode) | `192.168.4.1/24` | ESP32 node hotspot |
| MQTT TCP | `192.168.4.1:1883` | ESP32 nodes + Telegraf |
| MQTT WebSocket | `192.168.4.1:9001` | Grafana MQTT Live plugin |
| InfluxDB | `localhost:8086` | Internal only |
| Grafana | `192.168.4.1:3000` | Dashboard access |

## Security Considerations

- MQTT broker currently allows anonymous connections (appropriate for isolated hotspot)
- InfluxDB and Grafana credentials are stored in `grafana/.env` (git-ignored)
- Credentials are passed to services via `/etc/default/telegraf` and `/etc/default/grafana-server` (mode 640, root-owned)
- For production hardening: enable MQTT authentication, rotate the InfluxDB token, and restrict Grafana to the hotspot subnet

## Scalability

- Mosquitto handles thousands of concurrent connections
- InfluxDB efficiently stores time-series data with configurable retention
- Telegraf's `winter-river/#` wildcard subscription automatically captures new node IDs
- Grafana dashboards can use a `node_id` variable with panel repeating to visualize any number of nodes dynamically

## References

- [Mosquitto Documentation](https://mosquitto.org/documentation/)
- [InfluxDB 2 Documentation](https://docs.influxdata.com/influxdb/v2/)
- [Telegraf MQTT Consumer Plugin](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/mqtt_consumer)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Grafana Documentation](https://grafana.com/docs/)
- [grafana-mqtt-datasource](https://grafana.com/grafana/plugins/grafana-mqtt-datasource/)
