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
    postgresql postgresql-client libpq-dev \
    python3 python3-pip python3-venv \
    wget gpg curl dirmngr

# ── InfluxDB 2 + Telegraf ────────────────────────────────────────────────────
echo "Installing InfluxDB 2 and Telegraf..."

# Debian Trixie uses sqv for apt signature verification, which requires
# OpenPGP certificate format (not GPG keybox). Export via gpg --export.
INFLUX_KEYRING="/usr/share/keyrings/influxdata-archive-keyring.gpg"
INFLUX_KEY_ID="AC10D7449F343ADCEFDDC2B6DA61C26A0585BD3B"
INFLUX_LIST="/etc/apt/sources.list.d/influxdata.list"

if [ ! -f "$INFLUX_KEYRING" ] || ! apt-key --keyring "$INFLUX_KEYRING" list "$INFLUX_KEY_ID" &>/dev/null; then
    # Ensure root's gnupg directory exists (dirmngr needs it for keyserver access)
    mkdir -p /root/.gnupg
    chmod 700 /root/.gnupg
    TMPRING=$(mktemp /tmp/influx-keyring-XXXXXX.gpg)
    gpg --no-default-keyring \
        --keyring "$TMPRING" \
        --keyserver hkps://keyserver.ubuntu.com \
        --recv-keys "$INFLUX_KEY_ID"
    gpg --no-default-keyring \
        --keyring "$TMPRING" \
        --export "$INFLUX_KEY_ID" \
        | tee "$INFLUX_KEYRING" > /dev/null
    chmod 644 "$INFLUX_KEYRING"
    rm -f "$TMPRING" "${TMPRING}~"
fi

echo "deb [signed-by=$INFLUX_KEYRING] https://repos.influxdata.com/debian stable main" \
    > "$INFLUX_LIST"

apt-get update -qq
apt-get install -y influxdb2 telegraf

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

# ── InfluxDB setup ───────────────────────────────────────────────────────────
echo "Configuring InfluxDB..."
systemctl enable influxdb
systemctl start influxdb

# Wait for InfluxDB to be ready
for i in $(seq 1 15); do
    if curl -sf http://localhost:8086/health > /dev/null 2>&1; then
        break
    fi
    sleep 1
done

# Run initial setup (idempotent — skips if already set up)
if influx setup \
    --username "$INFLUXDB_ADMIN_USER" \
    --password "$INFLUXDB_ADMIN_PASSWORD" \
    --token "$INFLUXDB_ADMIN_TOKEN" \
    --org iot-project \
    --bucket mqtt_metrics \
    --force 2>/dev/null; then
    echo "InfluxDB initial setup complete."
else
    echo "InfluxDB already set up (or setup failed — check 'influx setup' manually)."
fi

# ── Telegraf ─────────────────────────────────────────────────────────────────
echo "Configuring Telegraf..."
# Inject InfluxDB token into Telegraf's environment
echo "INFLUX_TOKEN=$INFLUXDB_ADMIN_TOKEN" > /etc/default/telegraf
# Deploy project telegraf config
cp "$PROJECT_DIR/grafana/telegraf.conf" /etc/telegraf/telegraf.conf
systemctl enable telegraf
systemctl restart telegraf

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
    sudo -u postgres psql -d winter_river < "$PROJECT_DIR/scripts/init_db.sql"
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

# Inject InfluxDB token + Grafana credentials into grafana-server environment
# so datasource.yml can resolve ${INFLUXDB_TOKEN} at startup
cat > /etc/default/grafana-server <<GRAFENV
GF_SECURITY_ADMIN_USER=$GF_SECURITY_ADMIN_USER
GF_SECURITY_ADMIN_PASSWORD=$GF_SECURITY_ADMIN_PASSWORD
INFLUXDB_TOKEN=$INFLUXDB_ADMIN_TOKEN
GRAFENV

# Install MQTT Live datasource plugin (for real-time WebSocket panels)
grafana-cli plugins install grafana-mqtt-datasource 2>/dev/null || true

systemctl enable grafana-server
systemctl restart grafana-server

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
echo "Services:"
for svc in winter-river-hotspot mosquitto influxdb telegraf postgresql grafana-server; do
    if systemctl is-active --quiet "$svc" 2>/dev/null; then
        echo "  ✔  $svc: RUNNING"
    else
        sub=$(systemctl show -p SubState --value "$svc" 2>/dev/null || true)
        if [ "$sub" = "exited" ]; then
            echo "  ✔  $svc: OK (oneshot)"
        else
            echo "  ✘  $svc: STOPPED"
        fi
    fi
done
# NTP (service name varies)
{ systemctl is-active --quiet ntpsec 2>/dev/null \
    || systemctl is-active --quiet ntp 2>/dev/null \
    || systemctl is-active --quiet ntpd 2>/dev/null; } \
    && echo "  ✔  ntp: RUNNING" || echo "  ✘  ntp: STOPPED"
echo ""
echo "  Hotspot SSID : WinterRiver-AP"
echo "  Password     : winterriver"
echo "  Gateway      : 192.168.4.1"
echo "  MQTT broker  : 192.168.4.1:1883"
echo "  InfluxDB     : http://localhost:8086  (org: iot-project, bucket: mqtt_metrics)"
echo "  Grafana      : http://192.168.4.1:3000"
echo "  PostgreSQL   : localhost:5432  db=winter_river"
echo ""
echo "Next: copy broker/config.sample.toml to broker/config.toml and set DB password."
echo "Then: flash ESP32 nodes and power them on."
