#!/bin/bash
# Raspberry Pi Initial Setup Script
# Sets up the complete Winter River IoT system on Raspberry Pi
#
# Run once after cloning the repo:
#   sudo ./scripts/setup_pi.sh
#
# Re-running is safe — all steps are idempotent.

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

# ── Load credentials ──────────────────────────────────────────────────────────
ENV_FILE="$PROJECT_DIR/grafana/.env"
if [ -f "$ENV_FILE" ]; then
    echo "Loading credentials from $ENV_FILE ..."
    set -a
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    set +a
else
    echo ""
    echo "WARNING: $ENV_FILE not found — using placeholder credentials."
    echo "         Copy grafana/.env.sample to grafana/.env and set real passwords"
    echo "         before running this script in production!"
    echo ""
fi

# Apply defaults for any variable not already set by .env
# These are intentionally not 'changeme' so they are clearly placeholders
# but still unique enough to not collide with other defaults.
INFLUXDB_ADMIN_USER="${INFLUXDB_ADMIN_USER:-influx-admin}"
INFLUXDB_ADMIN_PASSWORD="${INFLUXDB_ADMIN_PASSWORD:-WinterRiverInflux_CHANGE_ME}"
# Token must be at least 32 chars; derive a host-unique default if unset
INFLUXDB_ADMIN_TOKEN="${INFLUXDB_ADMIN_TOKEN:-WinterRiverToken_$(hostname | sha256sum | cut -c1-24)}"
GF_SECURITY_ADMIN_USER="${GF_SECURITY_ADMIN_USER:-admin}"
GF_SECURITY_ADMIN_PASSWORD="${GF_SECURITY_ADMIN_PASSWORD:-WinterRiverGrafana_CHANGE_ME}"

# ── System packages ───────────────────────────────────────────────────────────
echo "Installing system packages..."
apt-get update -qq
# Note: Debian Trixie uses ntpsec instead of ntp/ntpdate
apt-get install -y \
    mosquitto mosquitto-clients \
    ntpsec ntpsec-ntpdate \
<<<<<<< HEAD
    postgresql postgresql-client \
    python3 python3-pip python3-venv \
    wget gpg
=======
    curl gnupg

# ── InfluxData apt repository (InfluxDB 2 + Telegraf) ────────────────────────
echo "Configuring InfluxData apt repository..."
INFLUX_GPG="/etc/apt/trusted.gpg.d/influxdata.gpg"
INFLUX_LIST="/etc/apt/sources.list.d/influxdata.list"

if [ ! -f "$INFLUX_LIST" ]; then
    curl -fsSL https://repos.influxdata.com/influxdata-archive_compat.key \
        | gpg --dearmor -o "$INFLUX_GPG"
    echo "deb [arch=arm64 signed-by=${INFLUX_GPG}] https://repos.influxdata.com/debian stable main" \
        > "$INFLUX_LIST"
    apt-get update -qq
else
    echo "  InfluxData repo already configured — skipping."
fi

# ── InfluxDB 2 ────────────────────────────────────────────────────────────────
echo "Installing InfluxDB 2..."
apt-get install -y influxdb2

echo "Enabling and starting influxdb..."
systemctl enable influxdb
systemctl start influxdb

# Wait for InfluxDB HTTP API to be ready (up to 60 s)
echo "  Waiting for InfluxDB to become ready..."
for i in $(seq 1 30); do
    influx ping 2>/dev/null && break
    sleep 2
done
influx ping 2>/dev/null || { echo "ERROR: InfluxDB did not start in time."; exit 1; }

# Non-interactive initial setup (idempotent — silently skips if already done)
echo "Running InfluxDB initial setup..."
influx setup \
    --username  "$INFLUXDB_ADMIN_USER" \
    --password  "$INFLUXDB_ADMIN_PASSWORD" \
    --org       iot-project \
    --bucket    mqtt_metrics \
    --token     "$INFLUXDB_ADMIN_TOKEN" \
    --retention 0 \
    --force 2>&1 \
    && echo "  InfluxDB setup complete." \
    || echo "  InfluxDB already initialized — skipping."

# ── Telegraf ─────────────────────────────────────────────────────────────────
echo "Installing Telegraf..."
apt-get install -y telegraf

# Write telegraf config (copy from repo, no templating needed — token via env)
echo "  Copying telegraf.conf..."
cp "$PROJECT_DIR/grafana/telegraf.conf" /etc/telegraf/telegraf.conf

# Inject the InfluxDB token into Telegraf's service environment
echo "  Writing /etc/default/telegraf ..."
cat > /etc/default/telegraf <<EOF
# Written by scripts/setup_pi.sh — re-run to refresh
INFLUX_TOKEN=${INFLUXDB_ADMIN_TOKEN}
EOF
chmod 640 /etc/default/telegraf

echo "Enabling and starting telegraf..."
systemctl enable telegraf
systemctl restart telegraf

# ── Grafana (already installed natively — configure only) ────────────────────
echo "Configuring Grafana..."

# Install MQTT Live datasource plugin
echo "  Installing grafana-mqtt-datasource plugin..."
grafana-cli plugins install grafana-mqtt-datasource 2>/dev/null \
    || echo "  (plugin already installed or grafana-cli unavailable — check manually)"

# Provision datasources and dashboards
echo "  Copying provisioning files..."
mkdir -p /etc/grafana/provisioning/datasources \
         /etc/grafana/provisioning/dashboards \
         /var/lib/grafana/dashboards

cp -r "$PROJECT_DIR/grafana/provisioning/datasources/." /etc/grafana/provisioning/datasources/
cp -r "$PROJECT_DIR/grafana/provisioning/dashboards/."  /etc/grafana/provisioning/dashboards/
cp -r "$PROJECT_DIR/grafana/dashboards/."               /var/lib/grafana/dashboards/

chown -R grafana:grafana \
    /etc/grafana/provisioning/datasources \
    /etc/grafana/provisioning/dashboards \
    /var/lib/grafana/dashboards

# Inject secrets and dashboard path into Grafana's service environment.
# Grafana expands ${VAR} in provisioning YAML files from its process environment.
echo "  Writing /etc/default/grafana-server ..."
cat > /etc/default/grafana-server <<EOF
# Written by scripts/setup_pi.sh — re-run to refresh
GF_SECURITY_ADMIN_USER=${GF_SECURITY_ADMIN_USER}
GF_SECURITY_ADMIN_PASSWORD=${GF_SECURITY_ADMIN_PASSWORD}
INFLUXDB_TOKEN=${INFLUXDB_ADMIN_TOKEN}
GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH=/var/lib/grafana/dashboards/broker-overview.json
EOF
chmod 640 /etc/default/grafana-server

echo "Enabling and restarting grafana-server..."
systemctl enable grafana-server
systemctl restart grafana-server
>>>>>>> d8a14531abfd618bee7d40d106aa0bec62e21533

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
systemctl is-active influxdb    2>/dev/null \
    && echo "  influxdb:         RUNNING" || echo "  influxdb:         STOPPED"
systemctl is-active telegraf    2>/dev/null \
    && echo "  telegraf:         RUNNING" || echo "  telegraf:         STOPPED"
systemctl is-active grafana-server 2>/dev/null \
    && echo "  grafana-server:   RUNNING" || echo "  grafana-server:   STOPPED"
systemctl is-active winter-river-hotspot \
<<<<<<< HEAD
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
=======
    && echo "  hotspot:          RUNNING" || echo "  hotspot:          STOPPED"
systemctl is-active mosquitto \
    && echo "  mosquitto:        RUNNING" || echo "  mosquitto:        STOPPED"
{ systemctl is-active ntpsec 2>/dev/null \
    || systemctl is-active ntp 2>/dev/null \
    || systemctl is-active ntpd 2>/dev/null; } \
    && echo "  ntp:              RUNNING" || echo "  ntp:              STOPPED"
echo ""
echo "  Hotspot SSID   : WinterRiver-AP"
echo "  Password       : winterriver"
echo "  Gateway        : 192.168.4.1"
echo "  MQTT (TCP)     : 192.168.4.1:1883"
echo "  MQTT (WS)      : ws://192.168.4.1:9001"
echo "  InfluxDB       : http://192.168.4.1:8086"
echo "  Grafana        : http://192.168.4.1:3000"
echo ""
if [ ! -f "$ENV_FILE" ]; then
    echo "  !! ACTION REQUIRED: Set real credentials in grafana/.env !!"
    echo "     Copy: grafana/.env.sample → grafana/.env"
    echo "     Then re-run: sudo ./scripts/setup_pi.sh"
    echo ""
fi
echo "All services start automatically on every reboot."
echo "Next: flash ESP32 nodes, then power them on."
>>>>>>> d8a14531abfd618bee7d40d106aa0bec62e21533
