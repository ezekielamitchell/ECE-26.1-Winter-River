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
# We overwrite the main mosquitto.conf entirely to avoid duplicate listener
# directives that occur when conf.d drop-ins conflict with the default config.
echo "Creating mosquitto configuration..."
mkdir -p /var/lib/mosquitto /var/log/mosquitto /run/mosquitto
chown mosquitto:mosquitto /var/lib/mosquitto /var/log/mosquitto /run/mosquitto

cat > /etc/mosquitto/mosquitto.conf <<EOF
# Winter River MQTT Broker Configuration
# Managed by deploy/mosquitto_setup.sh — do not edit manually

pid_file /run/mosquitto/mosquitto.pid

# Listen on all interfaces, port 1883
listener 1883 0.0.0.0
allow_anonymous true

# Persistence — retain messages across restarts
persistence true
persistence_location /var/lib/mosquitto/

# Logging
log_dest file /var/log/mosquitto/mosquitto.log
log_type error
log_type warning
log_type notice
log_type information
log_timestamp true
EOF

# Enable and start mosquitto
echo "Enabling and starting mosquitto service..."
systemctl enable mosquitto
# Stop first cleanly in case it was already running with old config
systemctl stop mosquitto 2>/dev/null || true
sleep 1
systemctl start mosquitto

# Verify service is running
echo "Verifying mosquitto service status..."
sleep 1
if ! systemctl is-active --quiet mosquitto; then
    echo "ERROR: mosquitto failed to start. Full log:"
    journalctl -u mosquitto -n 30 --no-pager
    exit 1
fi
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
