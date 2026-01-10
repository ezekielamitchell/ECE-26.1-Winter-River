#!/bin/bash
# Raspberry Pi Initial Setup Script
# Sets up the complete IoT system on Raspberry Pi

set -e  # Exit on error

echo "========================================="
echo "Raspberry Pi IoT System Setup"
echo "========================================="

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo"
    exit 1
fi

# TODO: Update system packages
echo "Updating system packages..."
# apt update && apt upgrade -y

# TODO: Install system dependencies
echo "Installing system dependencies..."
# apt install -y git python3 python3-pip python3-venv \
#     mosquitto mosquitto-clients \
#     docker.io docker-compose

# TODO: Add user to docker group
# usermod -aG docker pi

# TODO: Set up project directory
echo "Setting up project directory..."
# PROJECT_DIR="/home/pi/iot-mqtt-project"
# if [ ! -d "$PROJECT_DIR" ]; then
#     echo "Please clone the repository to $PROJECT_DIR first"
#     exit 1
# fi

# TODO: Set up Python virtual environment
echo "Setting up Python virtual environment..."
# cd "$PROJECT_DIR"
# python3 -m venv venv
# source venv/bin/activate
# pip install -r requirements.txt
# pip install -r requirements-dev.txt

# TODO: Configure broker
echo "Configuring MQTT broker..."
# cd "$PROJECT_DIR/broker"
# cp config.sample.toml config.toml
# echo "Please edit broker/config.toml with your settings"

# TODO: Run mosquitto setup
echo "Setting up mosquitto broker..."
# cd "$PROJECT_DIR/deploy"
# chmod +x mosquitto_setup.sh
# ./mosquitto_setup.sh

# TODO: Set up Grafana stack
echo "Setting up Grafana stack..."
# cd "$PROJECT_DIR/grafana"
# cp .env.sample .env
# echo "Please edit grafana/.env with your settings"
# docker-compose up -d

# TODO: Set up systemd service (optional)
# cp "$PROJECT_DIR/deploy/mqtt-broker.service" /etc/systemd/system/
# systemctl daemon-reload
# systemctl enable mqtt-broker
# systemctl start mqtt-broker

echo ""
echo "========================================="
echo "Setup Script Complete!"
echo "========================================="
echo "TODO: Uncomment and implement the setup steps above"
echo ""
echo "Next steps:"
echo "1. Edit broker/config.toml with your MQTT settings"
echo "2. Edit grafana/.env with your credentials"
echo "3. Configure ESP32 nodes with WiFi and broker IP"
echo "4. Deploy ESP32 firmware using PlatformIO"
echo "5. Access Grafana at http://$(hostname -I | awk '{print $1}'):3000"
