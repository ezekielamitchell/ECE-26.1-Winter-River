# Deployment Guide

This guide covers deploying the IoT system components on Raspberry Pi and ESP32 nodes.

## Prerequisites

### Hardware Requirements

- **Raspberry Pi** (Model 3B+ or newer recommended)
  - Minimum 1GB RAM
  - 16GB+ microSD card
  - Network connectivity (Ethernet or WiFi)

- **ESP32 DevKitC v1** (1-20 units)
  - USB cable for programming
  - Power supply (USB or external)

### Software Requirements

- **Raspberry Pi**:
  - Raspberry Pi OS (64-bit recommended)
  - Python 3.9+
  - Docker & Docker Compose (for Grafana stack)
  - mosquitto MQTT broker

- **Development Machine**:
  - PlatformIO Core or PlatformIO IDE
  - Python 3.9+
  - Git

## Part 1: Raspberry Pi Setup

### 1.1 Initial System Setup

```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Install required system packages
sudo apt install -y git python3 python3-pip python3-venv

# Clone the repository
cd ~
git clone https://github.com/yourusername/iot-mqtt-project.git
cd iot-mqtt-project
```

### 1.2 Install mosquitto MQTT Broker

Use the provided setup script:

```bash
cd deploy
chmod +x mosquitto_setup.sh
sudo ./mosquitto_setup.sh
```

Or install manually:

```bash
# Install mosquitto
sudo apt install -y mosquitto mosquitto-clients

# Enable mosquitto service
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

# Verify it's running
sudo systemctl status mosquitto
```

### 1.3 Configure mosquitto

TODO: Configure mosquitto with authentication and ACLs

```bash
# Edit mosquitto configuration
sudo nano /etc/mosquitto/mosquitto.conf

# Reload configuration
sudo systemctl restart mosquitto
```

### 1.4 Install Python Management Utilities

```bash
# Create virtual environment
cd ~/iot-mqtt-project
python3 -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt

# Copy and configure settings
cp broker/config.sample.toml broker/config.toml
nano broker/config.toml  # Edit configuration
```

### 1.5 Set Up Systemd Service (Optional)

```bash
# Copy service file
sudo cp deploy/mqtt-broker.service /etc/systemd/system/

# Edit service file with correct paths
sudo nano /etc/systemd/system/mqtt-broker.service

# Enable and start service
sudo systemctl enable mqtt-broker
sudo systemctl start mqtt-broker
sudo systemctl status mqtt-broker
```

### 1.6 Deploy Grafana Stack

```bash
# Install Docker and Docker Compose
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo apt install -y docker-compose

# Add user to docker group
sudo usermod -aG docker $USER
# Log out and back in for group change to take effect

# Configure Grafana stack
cd grafana
cp .env.sample .env
nano .env  # Edit configuration

# Start the stack
docker-compose up -d

# Verify services are running
docker-compose ps
```

Access Grafana at `http://<raspberry-pi-ip>:3000`

### 1.7 Verify Deployment

```bash
# Test MQTT broker
mosquitto_pub -h localhost -t test/topic -m "Hello MQTT"
mosquitto_sub -h localhost -t test/topic

# Check logs
sudo journalctl -u mosquitto -f
docker-compose logs -f grafana
```

## Part 2: ESP32 Firmware Deployment

### 2.1 Configure Firmware

For each ESP32 node:

```bash
cd esp
cp include/config.sample.h include/config.h
```

Edit `include/config.h` with:
- WiFi SSID and password
- Raspberry Pi IP address (MQTT broker)
- Unique device ID for each node

### 2.2 Build and Upload Firmware

```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Build the firmware
cd esp
pio run

# Upload to connected ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### 2.3 Deploy Multiple Nodes

For each additional node:

1. Update `DEVICE_ID` in `include/config.h`
2. Update `MQTT_CLIENT_ID`
3. Update topic names to use unique node ID
4. Build and upload

Example for Node 02:
```cpp
#define DEVICE_ID "esp32_node_02"
#define MQTT_CLIENT_ID "ESP32Client_02"
#define TOPIC_TELEMETRY "iot/node/02/telemetry"
#define TOPIC_COMMAND "iot/node/02/command"
```

## Part 3: Network Configuration

### 3.1 Static IP for Raspberry Pi (Recommended)

Edit `/etc/dhcpcd.conf`:

```bash
sudo nano /etc/dhcpcd.conf
```

Add:
```
interface eth0  # or wlan0 for WiFi
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=192.168.1.1 8.8.8.8
```

Restart networking:
```bash
sudo systemctl restart dhcpcd
```

### 3.2 Firewall Configuration

```bash
# Install ufw (if not installed)
sudo apt install -y ufw

# Allow SSH
sudo ufw allow 22/tcp

# Allow MQTT
sudo ufw allow 1883/tcp

# Allow Grafana
sudo ufw allow 3000/tcp

# Enable firewall
sudo ufw enable
```

## Part 4: Testing the System

### 4.1 Test MQTT Communication

From development machine:

```bash
# Subscribe to all node telemetry
mosquitto_sub -h <raspberry-pi-ip> -t "iot/node/+/telemetry" -v

# Publish a test command
mosquitto_pub -h <raspberry-pi-ip> -t "iot/node/01/command" -m '{"command":"test"}'
```

### 4.2 Verify Grafana Dashboards

1. Open Grafana at `http://<raspberry-pi-ip>:3000`
2. Login (default: admin/admin)
3. Navigate to dashboards
4. Verify data is appearing from ESP32 nodes

### 4.3 Check InfluxDB Data

```bash
# Access InfluxDB container
docker exec -it influxdb influx

# Query data (InfluxDB v2 uses Flux)
# TODO: Add Flux query examples
```

## Part 5: Production Checklist

### Before Production Deployment

- [ ] Change all default passwords (Grafana, InfluxDB, MQTT)
- [ ] Enable MQTT authentication
- [ ] Configure TLS/SSL for MQTT
- [ ] Set up automatic backups
- [ ] Configure log rotation
- [ ] Set up monitoring/alerting
- [ ] Document node locations and IDs
- [ ] Create recovery procedures
- [ ] Test failover scenarios
- [ ] Update system documentation

### Security Hardening

- [ ] Disable SSH password authentication (use keys only)
- [ ] Enable automatic security updates
- [ ] Configure fail2ban
- [ ] Restrict MQTT broker access by IP
- [ ] Use strong passwords for all services
- [ ] Regular security audits

## Troubleshooting

### mosquitto Not Starting

```bash
# Check logs
sudo journalctl -u mosquitto -n 50

# Verify configuration
mosquitto -c /etc/mosquitto/mosquitto.conf -v

# Check port availability
sudo netstat -tulpn | grep 1883
```

### ESP32 Cannot Connect to WiFi

- Verify SSID and password in `config.h`
- Check WiFi signal strength
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Check serial monitor for error messages

### ESP32 Cannot Connect to MQTT Broker

- Verify Raspberry Pi IP address in `config.h`
- Check firewall rules
- Verify mosquitto is running: `sudo systemctl status mosquitto`
- Test connectivity: `ping <raspberry-pi-ip>` from ESP32 network

### Grafana Shows No Data

- Verify Telegraf is consuming MQTT messages
- Check InfluxDB has data: `docker exec -it influxdb influx`
- Verify data source configuration in Grafana
- Check Telegraf logs: `docker-compose logs telegraf`

## Maintenance

### Regular Tasks

- **Daily**: Check Grafana dashboards for anomalies
- **Weekly**: Review system logs
- **Monthly**:
  - Update system packages
  - Verify backups
  - Check disk space usage
  - Review security logs

### Backup Procedures

```bash
# Backup InfluxDB data
docker exec influxdb influxd backup -portable /backup
docker cp influxdb:/backup ./influxdb-backup-$(date +%Y%m%d)

# Backup Grafana dashboards
docker exec grafana grafana-cli admin export-all > grafana-backup-$(date +%Y%m%d).json

# Backup mosquitto config
sudo cp /etc/mosquitto/mosquitto.conf ~/backups/mosquitto.conf.$(date +%Y%m%d)
```

## References

- [Raspberry Pi Documentation](https://www.raspberrypi.org/documentation/)
- [mosquitto Configuration](https://mosquitto.org/man/mosquitto-conf-5.html)
- [PlatformIO CLI Reference](https://docs.platformio.org/en/latest/core/index.html)
- [Docker Compose Documentation](https://docs.docker.com/compose/)
