#!/bin/bash
# Raspberry Pi Initial Setup Script
# Sets up the complete Winter River IoT system on Raspberry Pi
#
# Run once after cloning the repo:
#   sudo ./scripts/setup_pi.sh

set -euo pipefail

echo "========================================="
echo "  Raspberry Pi IoT System Setup"
echo "========================================="

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo"
    exit 1
fi

ACTUAL_USER="${SUDO_USER:-pi}"
PROJECT_DIR="/home/$ACTUAL_USER/ECE-26.1-Winter-River"

# Verify project directory exists
if [ ! -d "$PROJECT_DIR" ]; then
    echo "ERROR: Project directory not found at $PROJECT_DIR"
    echo "Please clone the repository first:"
    echo "  git clone <repo-url> $PROJECT_DIR"
    exit 1
fi

# ── System packages ───────────────────────────────────────────────────────────
echo "Installing system packages..."
apt update -qq
# Note: Debian Trixie uses ntpsec instead of ntp/ntpdate
apt install -y \
    mosquitto mosquitto-clients \
    ntpsec ntpsec-ntpdate

# ── Mosquitto MQTT broker ─────────────────────────────────────────────────────
echo "Configuring mosquitto..."
chmod +x "$PROJECT_DIR/deploy/mosquitto_setup.sh"
"$PROJECT_DIR/deploy/mosquitto_setup.sh"

# ── WiFi hotspot ──────────────────────────────────────────────────────────────
echo "Installing hotspot systemd service..."
chmod +x "$PROJECT_DIR/scripts/setup_hotspot.sh"
# Copy service file, substituting actual project path if not default
sed "s|/home/pi/ECE-26.1-Winter-River|$PROJECT_DIR|g" \
    "$PROJECT_DIR/deploy/winter-river-hotspot.service" \
    > /etc/systemd/system/winter-river-hotspot.service

echo "Starting WiFi hotspot..."
"$PROJECT_DIR/scripts/setup_hotspot.sh" start

# ── NTP (time sync for ESP32 nodes) ──────────────────────────────────────────
echo "Configuring NTP..."
# Allow hotspot subnet clients to query the Pi's NTP service
# Debian Trixie: /etc/ntpsec/ntp.conf  |  older: /etc/ntp.conf
NTP_CONF=""
[ -f /etc/ntpsec/ntp.conf ] && NTP_CONF="/etc/ntpsec/ntp.conf"
[ -z "$NTP_CONF" ] && [ -f /etc/ntp.conf ] && NTP_CONF="/etc/ntp.conf"
if [ -n "$NTP_CONF" ] && ! grep -q "192.168.4.0" "$NTP_CONF"; then
    echo "restrict 192.168.4.0 mask 255.255.255.0 nomodify notrap" >> "$NTP_CONF"
fi
systemctl restart ntpsec 2>/dev/null \
    || systemctl restart ntp 2>/dev/null \
    || systemctl restart ntpd 2>/dev/null \
    || true

# ── Enable all services for auto-boot ────────────────────────────────────────
echo "Enabling services for auto-boot..."
systemctl daemon-reload
systemctl enable winter-river-hotspot
systemctl enable mosquitto
systemctl enable ntpsec 2>/dev/null || systemctl enable ntp 2>/dev/null || true

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "========================================="
echo "  Setup Complete!"
echo "========================================="
echo ""
echo "Services enabled for auto-boot:"
systemctl is-active winter-river-hotspot \
    && echo "  hotspot:    RUNNING" || echo "  hotspot:    STOPPED"
systemctl is-active mosquitto \
    && echo "  mosquitto:  RUNNING" || echo "  mosquitto:  STOPPED"
{ systemctl is-active ntpsec 2>/dev/null \
    || systemctl is-active ntp 2>/dev/null \
    || systemctl is-active ntpd 2>/dev/null; } \
    && echo "  ntp:        RUNNING" || echo "  ntp:        STOPPED"
echo ""
echo "  Hotspot SSID : WinterRiver-AP"
echo "  Password     : winterriver"
echo "  Gateway      : 192.168.4.1"
echo "  MQTT broker  : 192.168.4.1:1883"
echo ""
echo "All services start automatically on every reboot."
echo "Next: flash ESP32 nodes, then power them on."
