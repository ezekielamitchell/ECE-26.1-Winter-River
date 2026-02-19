#!/bin/bash
# mosquitto MQTT Broker Setup Script for Raspberry Pi
# This script installs and configures mosquitto broker

set -euo pipefail

echo "========================================="
echo "mosquitto MQTT Broker Setup"
echo "========================================="

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo"
    exit 1
fi

# Install mosquitto
echo "Installing mosquitto and mosquitto-clients..."
apt update
apt install -y mosquitto mosquitto-clients

# Create mosquitto configuration
echo "Creating mosquitto configuration..."
mkdir -p /etc/mosquitto/conf.d
cat > /etc/mosquitto/conf.d/winter-river.conf <<EOF
# Winter River MQTT Broker Configuration
listener 1883
allow_anonymous true

# Persistence
persistence true
persistence_location /var/lib/mosquitto/

# Logging
log_dest file /var/log/mosquitto/mosquitto.log
log_dest stdout
log_type all
EOF

# Ensure log directory exists with correct ownership
mkdir -p /var/log/mosquitto
chown mosquitto:mosquitto /var/log/mosquitto

# Enable and start mosquitto
echo "Enabling and starting mosquitto service..."
systemctl enable mosquitto
systemctl restart mosquitto

# Verify service is running
echo "Verifying mosquitto service status..."
systemctl status mosquitto --no-pager

# Test MQTT broker
echo "Testing MQTT broker..."
sleep 1
timeout 3 mosquitto_sub -h localhost -t test/ping -C 1 &
SUB_PID=$!
sleep 0.5
mosquitto_pub -h localhost -t test/ping -m "ok"
wait $SUB_PID && echo "Broker test: PASSED" || echo "Broker test: FAILED (check journalctl -u mosquitto)"

echo ""
echo "========================================="
echo "mosquitto setup complete!"
echo "========================================="
echo "Broker listening on 0.0.0.0:1883"
echo "Logs: /var/log/mosquitto/mosquitto.log"
echo "Test: mosquitto_pub -h 192.168.4.1 -t winter-river/test -m hello"
