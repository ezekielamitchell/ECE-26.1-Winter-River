# System Architecture

## Overview

This IoT system consists of three main components:

1. **MQTT Broker** (Raspberry Pi running mosquitto)
2. **ESP32 Nodes** (DevKitC v1 running C++ firmware via PlatformIO)
3. **Visualization Layer** (Grafana dashboards with InfluxDB backend)

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      Raspberry Pi                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  mosquitto   │  │   Python     │  │   Grafana    │      │
│  │ MQTT Broker  │◄─┤  Management  │  │  + InfluxDB  │      │
│  │  (port 1883) │  │   Utilities  │  │  + Telegraf  │      │
│  └──────┬───────┘  └──────────────┘  └──────────────┘      │
│         │                                      ▲             │
└─────────┼──────────────────────────────────────┼─────────────┘
          │                                      │
          │ MQTT Protocol                  Data Bridge
          │ (pub/sub)                      (MQTT→InfluxDB)
          │                                      │
    ┌─────┴───────────────────────┬──────────────┘
    │                             │
    ▼                             ▼
┌───────────┐  ┌───────────┐  ┌───────────┐
│  ESP32    │  │  ESP32    │  │  ESP32    │
│  Node 01  │  │  Node 02  │  │  Node 03  │
│           │  │           │  │           │
│ - Sensors │  │ - Sensors │  │ - Sensors │
│ - WiFi    │  │ - WiFi    │  │ - WiFi    │
│ - MQTT    │  │ - MQTT    │  │ - MQTT    │
└───────────┘  └───────────┘  └───────────┘
```

## Component Details

### MQTT Broker (mosquitto on Raspberry Pi)

- **Role**: Central message broker for pub/sub messaging
- **Port**: 1883 (MQTT), optionally 8883 (MQTT over TLS)
- **Responsibilities**:
  - Accept connections from ESP32 nodes
  - Route messages between publishers and subscribers
  - Maintain client sessions and subscriptions
  - Provide QoS levels (0, 1, 2)

### Python Management Utilities

- **Role**: Broker management and monitoring tools
- **Library**: paho-mqtt
- **Responsibilities**:
  - Subscribe to telemetry topics for logging/processing
  - Publish commands to ESP32 nodes
  - Monitor broker health
  - Bridge to external systems (optional)

### ESP32 Nodes (DevKitC v1)

- **Role**: Edge devices with sensors/actuators
- **Platform**: PlatformIO with Arduino framework
- **Responsibilities**:
  - Connect to WiFi network
  - Establish MQTT connection to broker
  - Publish telemetry data (sensors, status)
  - Subscribe to command topics
  - Execute commands received from broker
  - Handle reconnection logic

### Grafana Visualization Stack

- **Components**:
  - **Grafana**: Web-based dashboard for visualization
  - **InfluxDB**: Time-series database for metrics storage
  - **Telegraf**: MQTT consumer that bridges messages to InfluxDB

- **Responsibilities**:
  - Consume MQTT messages via Telegraf
  - Store time-series data in InfluxDB
  - Provide real-time dashboards for broker and node metrics
  - Alert on anomalies (optional)

## Communication Flow

### Telemetry Publishing (ESP32 → Broker → Grafana)

1. ESP32 reads sensor data
2. ESP32 publishes JSON payload to `iot/node/{id}/telemetry`
3. Broker receives and distributes message to subscribers
4. Telegraf (subscribed to telemetry topics) receives message
5. Telegraf writes data to InfluxDB
6. Grafana queries InfluxDB and displays in dashboard

### Command Handling (Management → Broker → ESP32)

1. Management utility publishes command to `iot/node/{id}/command`
2. Broker routes message to subscribed ESP32 node
3. ESP32 receives command and executes action
4. ESP32 publishes acknowledgment/status update

## Topic Structure

```
iot/
├── broker/
│   ├── status          # Broker health metrics
│   └── stats           # Message throughput, connections
├── node/
│   ├── 01/
│   │   ├── telemetry  # Sensor data from node 01
│   │   ├── command    # Commands to node 01
│   │   └── status     # Node health (uptime, WiFi RSSI, etc.)
│   ├── 02/
│   │   ├── telemetry
│   │   ├── command
│   │   └── status
│   └── 03/
│       ├── telemetry
│       ├── command
│       └── status
```

## Data Format

### Telemetry Message Example

```json
{
  "node_id": "node_01",
  "timestamp": 1704844800,
  "sensors": {
    "temperature": 22.5,
    "humidity": 45.2
  },
  "wifi_rssi": -65,
  "uptime": 3600
}
```

### Command Message Example

```json
{
  "command": "set_led",
  "params": {
    "state": "on",
    "brightness": 128
  }
}
```

## Security Considerations

### TODO: Implement Security Features

- [ ] Enable MQTT authentication (username/password)
- [ ] Configure TLS/SSL for encrypted MQTT connections
- [ ] Implement client certificate authentication
- [ ] Network isolation (firewall rules)
- [ ] Secure credential storage (environment variables, secrets manager)
- [ ] Regular security updates for Raspberry Pi OS and packages

## Scalability

The current architecture supports:
- **Initial**: 3 ESP32 nodes
- **Target**: 20+ nodes

### Scalability Considerations

- mosquitto can handle thousands of concurrent connections
- InfluxDB efficiently stores time-series data with automatic retention policies
- Grafana dashboards can use variables and panel repeating for dynamic node visualization
- Consider message batching for high-frequency telemetry
- Monitor broker resource usage (CPU, memory, network bandwidth)

## Development Phases

### Phase 1: Basic Connectivity
- [ ] Set up mosquitto broker
- [ ] Implement basic ESP32 WiFi + MQTT connection
- [ ] Publish/subscribe test messages

### Phase 2: Telemetry Pipeline
- [ ] Add sensor reading on ESP32
- [ ] Implement JSON telemetry publishing
- [ ] Set up Telegraf → InfluxDB bridge
- [ ] Create basic Grafana dashboards

### Phase 3: Command & Control
- [ ] Implement command subscription on ESP32
- [ ] Add command execution logic
- [ ] Create management utility for sending commands

### Phase 4: Production Readiness
- [ ] Add authentication and encryption
- [ ] Implement OTA updates for ESP32
- [ ] Configure alerting in Grafana
- [ ] Add logging and monitoring
- [ ] Write comprehensive documentation

## References

- [mosquitto Documentation](https://mosquitto.org/documentation/)
- [MQTT Protocol Specification](https://mqtt.org/mqtt-specification/)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [paho-mqtt Python Client](https://www.eclipse.org/paho/index.php?page=clients/python/index.php)
- [Grafana Documentation](https://grafana.com/docs/)
