# ECE-26.1-Winter-River

IoT Environmental Monitoring System with MQTT and Grafana Visualization

## Overview

ECE-26.1-Winter-River is a distributed environmental monitoring system that uses ESP32 microcontrollers to collect temperature, humidity, and other sensor data, transmits it via MQTT protocol to a Python-based broker running on a Raspberry Pi, and visualizes the data in real-time using Grafana dashboards.

## System Architecture

```text
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   ESP32     │────▶│   MQTT      │────▶│  Grafana    │
│  Sensors    │     │   Broker    │     │  Dashboard  │
│  (Node 1-3) │     │ (Raspberry) │     │             │
└─────────────┘     └─────────────┘     └─────────────┘
```

### Components

- **ESP32 Nodes**: Three ESP32 microcontrollers equipped with DHT22/BME280 sensors
- **MQTT Broker**: Custom Python broker with PostgreSQL storage running on Raspberry Pi
- **Grafana**: Real-time data visualization with customizable dashboards
- **CI/CD**: Automated testing and deployment via GitHub Actions

## Features

- Real-time environmental monitoring (temperature, humidity, pressure)
- MQTT communication protocol for reliable data transmission
- PostgreSQL database for historical data storage
- Grafana dashboards with configurable alerts
- Automatic service management with systemd
- Remote deployment and updates via GitHub Actions

## Quick Start

### Prerequisites

- Raspberry Pi (4 or newer recommended) with Raspberry Pi OS
- 3x ESP32 development boards
- DHT22 or BME280 sensors
- WiFi network
- Python 3.9+
- Docker and Docker Compose (for Grafana)

### Installation

1. Clone the repository:

```bash
git clone https://github.com/yourusername/ECE-26.1-Winter-River.git
cd ECE-26.1-Winter-River
```

2. Set up the Raspberry Pi broker:

```bash
cd scripts
chmod +x setup_pi.sh
./setup_pi.sh
```

3. Configure the ESP32 nodes:

```bash
cd esp/node1
# Edit src/config.h with your WiFi credentials and broker IP
pio run -t upload
```

4. Deploy Grafana:

```bash
cd grafana
docker-compose up -d
```

5. Access Grafana at `http://raspberry-pi-ip:3000`
   - Default credentials: `admin` / `admin`

## Project Structure

```text
ECE-26.1-Winter-River/
├── broker/              # Python MQTT broker
│   ├── src/            # Source code
│   ├── tests/          # Unit tests
│   └── requirements.txt
├── esp/                # ESP32 firmware
│   ├── node1/          # Node 1 PlatformIO project
│   ├── node2/          # Node 2 PlatformIO project
│   └── node3/          # Node 3 PlatformIO project
├── grafana/            # Grafana configuration
│   ├── dashboards/     # Dashboard JSON files
│   ├── provisioning/   # Data source configs
│   └── docker-compose.yml
├── deploy/             # Deployment configs
│   └── broker.service  # systemd service file
├── scripts/            # Setup and deployment scripts
│   ├── setup_pi.sh     # Raspberry Pi setup
│   └── deploy.sh       # Deployment script
├── docs/               # Documentation
│   ├── architecture.md # System architecture
│   ├── deployment.md   # Deployment guide
│   └── api.md          # API reference
└── .github/            # CI/CD workflows
    └── workflows/
```

## Configuration

### Broker Configuration

Edit `broker/config.yaml`:

```yaml
mqtt:
  host: "0.0.0.0"
  port: 1883
database:
  host: "localhost"
  port: 5432
  name: "sensor_data"
```

### ESP32 Configuration

Edit `esp/nodeX/src/config.h`:

```cpp
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourPassword"
#define MQTT_BROKER "192.168.1.100"
#define MQTT_PORT 1883
```

## Development

### Running Tests

```bash
cd broker
python -m pytest tests/
```

### Building ESP32 Firmware

```bash
cd esp/node1
pio run
```

### Local Broker Development

```bash
cd broker
pip install -r requirements.txt
python src/main.py
```

## Deployment

### Manual Deployment

```bash
./scripts/deploy.sh
```

### Automated Deployment

Push to the `main` branch to trigger automatic deployment via GitHub Actions.

## Monitoring

### System Status

Check broker service status:
```bash
sudo systemctl status mqtt-broker
```

View logs:
```bash
sudo journalctl -u mqtt-broker -f
```

### Grafana Dashboards

Access dashboards at `http://raspberry-pi-ip:3000`:

- **Overview**: System-wide metrics
- **Node 1-3**: Individual node details
- **Alerts**: Active alerts and notifications

## Troubleshooting

### Broker won't start

```bash
# Check logs
sudo journalctl -u mqtt-broker -n 50

# Verify Python dependencies
cd broker
pip install -r requirements.txt
```

### ESP32 connection issues

1. Verify WiFi credentials in `config.h`
2. Check broker IP address and port
3. Monitor serial output: `pio device monitor`

### Grafana data source connection

1. Verify PostgreSQL is running: `sudo systemctl status postgresql`
2. Check database credentials in Grafana data source settings
3. Test connection from Grafana UI

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and the process for submitting pull requests.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built for ECE 26.1 course project
- Inspired by modern IoT architectures
- Thanks to the ESP-IDF and PlatformIO communities

## Contact

For questions or support, please open an issue on GitHub or contact the project maintainers.

## Roadmap

- [ ] Add support for additional sensor types
- [ ] Implement OTA (Over-The-Air) firmware updates
- [ ] Add mobile app for remote monitoring
- [ ] Implement machine learning for anomaly detection
- [ ] Add support for more ESP32 nodes (scalability)
