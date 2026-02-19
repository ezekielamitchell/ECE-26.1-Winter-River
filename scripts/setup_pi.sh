#!/bin/bash
# Raspberry Pi Initial Setup Script
# Sets up the complete IoT system on Raspberry Pi

set -euo pipefail

echo "========================================="
echo "Raspberry Pi IoT System Setup"
echo "========================================="

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo"
    exit 1
fi

ACTUAL_USER="${SUDO_USER:-pi}"
PROJECT_DIR="/home/$ACTUAL_USER/ECE-26.1-Winter-River"

# Update system packages
echo "Updating system packages..."
apt update && apt upgrade -y

# Install system dependencies
echo "Installing system dependencies..."
apt install -y git python3 python3-pip python3-venv \
    mosquitto mosquitto-clients \
    ntpdate ntp

# Verify project directory exists
if [ ! -d "$PROJECT_DIR" ]; then
    echo "ERROR: Project directory not found at $PROJECT_DIR"
    echo "Please clone the repository first:"
    echo "  git clone <repo-url> $PROJECT_DIR"
    exit 1
fi

# Set up Python virtual environment
echo "Setting up Python virtual environment..."
cd "$PROJECT_DIR"
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r broker/requirements.txt

# Configure broker
echo "Configuring MQTT broker..."
if [ ! -f "$PROJECT_DIR/broker/config.toml" ]; then
    cp "$PROJECT_DIR/broker/config.sample.toml" "$PROJECT_DIR/broker/config.toml"
    echo "broker/config.toml created from sample — review if needed"
fi

# Run mosquitto setup
echo "Setting up mosquitto broker..."
cd "$PROJECT_DIR/deploy"
chmod +x mosquitto_setup.sh
./mosquitto_setup.sh

# Set up WiFi hotspot
echo "Setting up WiFi hotspot..."
cd "$PROJECT_DIR/scripts"
chmod +x setup_hotspot.sh
./setup_hotspot.sh start

# Configure NTP server so ESP32 nodes can sync time from the Pi
echo "Configuring NTP server..."
# Allow LAN clients (192.168.4.0/24) to query the Pi's NTP service
if ! grep -q "192.168.4.0" /etc/ntp.conf 2>/dev/null; then
    echo "restrict 192.168.4.0 mask 255.255.255.0 nomodify notrap" >> /etc/ntp.conf
fi
systemctl restart ntp || systemctl restart ntpd || true

# Set up systemd service for broker management
echo "Setting up systemd service..."
# Update the service file paths to match this installation
sed -e "s|/home/pi/ECE-26.1-Winter-River|$PROJECT_DIR|g" \
    -e "s|User=pi|User=$ACTUAL_USER|g" \
    "$PROJECT_DIR/deploy/mqtt-broker.service" \
    > /etc/systemd/system/mqtt-broker.service
systemctl daemon-reload
systemctl enable mqtt-broker
systemctl start mqtt-broker

echo ""
echo "========================================="
echo "Setup Complete!"
echo "========================================="
echo ""
echo "Services status:"
systemctl is-active mosquitto   && echo "  mosquitto:     RUNNING" || echo "  mosquitto:     STOPPED"
systemctl is-active mqtt-broker && echo "  mqtt-broker:   RUNNING" || echo "  mqtt-broker:   STOPPED"
systemctl is-active ntp         && echo "  ntp:           RUNNING" || \
    (systemctl is-active ntpd   && echo "  ntpd:          RUNNING" || echo "  ntp:           STOPPED")
echo ""
echo "Hotspot: SSID=WinterRiver-AP  Password=winterriver  Gateway=192.168.4.1"
echo "MQTT broker listening on 192.168.4.1:1883"
echo ""
echo "Next: Flash ESP32 nodes with PlatformIO, then power them on."
