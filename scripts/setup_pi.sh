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
    ntpsec ntpsec-ntpdate \
    postgresql postgresql-client \
    python3 python3-pip python3-venv \
    wget gpg

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

# ── PostgreSQL ────────────────────────────────────────────────────────────────
echo "Configuring PostgreSQL..."
systemctl enable postgresql
systemctl start postgresql

# Create database and user (idempotent)
sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='winter_river'" \
    | grep -q 1 || sudo -u postgres psql -c "CREATE DATABASE winter_river;"

sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='grafana_reader'" \
    | grep -q 1 || sudo -u postgres psql -c "CREATE USER grafana_reader WITH PASSWORD 'changeme';"

# Initialize schema (idempotent via IF NOT EXISTS would require refactor;
# run only if tables don't exist yet)
TABLE_EXISTS=$(sudo -u postgres psql -d winter_river -tc \
    "SELECT 1 FROM information_schema.tables WHERE table_name='nodes';" | tr -d ' ')
if [ "$TABLE_EXISTS" != "1" ]; then
    echo "Initializing database schema..."
    sudo -u postgres psql -d winter_river -f "$PROJECT_DIR/scripts/init_db.sql"
else
    echo "Schema already exists, skipping init_db.sql"
fi

# Grant read access to grafana_reader
sudo -u postgres psql -d winter_river -c \
    "GRANT USAGE ON SCHEMA public TO grafana_reader;
     GRANT SELECT ON ALL TABLES IN SCHEMA public TO grafana_reader;
     ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO grafana_reader;"

# ── Grafana (apt install) ─────────────────────────────────────────────────────
echo "Installing Grafana..."
mkdir -p /etc/apt/keyrings/
wget -q -O - https://apt.grafana.com/gpg.key \
    | gpg --dearmor \
    | tee /etc/apt/keyrings/grafana.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/grafana.gpg] https://apt.grafana.com stable main" \
    | tee /etc/apt/sources.list.d/grafana.list
apt update -qq
apt install -y grafana

# Copy grafana.ini config
cp "$PROJECT_DIR/grafana/grafana.ini" /etc/grafana/grafana.ini

# Copy provisioning (datasource + dashboard auto-config)
cp -r "$PROJECT_DIR/grafana/provisioning/." /etc/grafana/provisioning/

# Copy dashboards if any exist
if [ -d "$PROJECT_DIR/grafana/dashboards" ] && \
   [ "$(ls -A "$PROJECT_DIR/grafana/dashboards" 2>/dev/null)" ]; then
    mkdir -p /var/lib/grafana/dashboards
    cp "$PROJECT_DIR/grafana/dashboards/"*.json /var/lib/grafana/dashboards/
    chown -R grafana:grafana /var/lib/grafana/dashboards
fi

systemctl enable grafana-server
systemctl start grafana-server

# ── Python broker venv ────────────────────────────────────────────────────────
echo "Setting up Python broker environment..."
VENV_DIR="$PROJECT_DIR/broker/venv"
sudo -u "$ACTUAL_USER" python3 -m venv "$VENV_DIR"
sudo -u "$ACTUAL_USER" "$VENV_DIR/bin/pip" install --quiet \
    -r "$PROJECT_DIR/broker/requirements.txt"

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
    && echo "  hotspot:     RUNNING" || echo "  hotspot:     STOPPED"
systemctl is-active mosquitto \
    && echo "  mosquitto:   RUNNING" || echo "  mosquitto:   STOPPED"
{ systemctl is-active ntpsec 2>/dev/null \
    || systemctl is-active ntp 2>/dev/null \
    || systemctl is-active ntpd 2>/dev/null; } \
    && echo "  ntp:         RUNNING" || echo "  ntp:         STOPPED"
systemctl is-active postgresql \
    && echo "  postgresql:  RUNNING" || echo "  postgresql:  STOPPED"
systemctl is-active grafana-server \
    && echo "  grafana:     RUNNING" || echo "  grafana:     STOPPED"
echo ""
echo "  Hotspot SSID : WinterRiver-AP"
echo "  Password     : winterriver"
echo "  Gateway      : 192.168.4.1"
echo "  MQTT broker  : 192.168.4.1:1883"
echo "  Grafana      : http://192.168.4.1:3000  (admin/admin)"
echo "  PostgreSQL   : localhost:5432  db=winter_river"
echo ""
echo "IMPORTANT: Change the grafana_reader password in:"
echo "  grafana/provisioning/datasources/datasource.yml"
echo "  and re-run: sudo cp grafana/provisioning/datasources/datasource.yml"
echo "              /etc/grafana/provisioning/datasources/datasource.yml"
echo "  then: sudo systemctl restart grafana-server"
echo ""
echo "Next: copy broker/config.sample.toml to broker/config.toml and set DB password."
echo "Then: flash ESP32 nodes and power them on."
