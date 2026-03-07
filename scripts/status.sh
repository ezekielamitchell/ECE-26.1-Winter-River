#!/bin/bash
# Winter River — System Status Script
# Shows the state of all services, hotspot, and connected nodes
#
# Usage:
#   ./status.sh          # Full status report
#   ./status.sh mqtt     # MQTT messages only (live tail)

set -euo pipefail

IFACE="wlan0"
CON_NAME="winter-river-hotspot"
BROKER="192.168.4.1"
MQTT_PORT="1883"

# ── Helpers ───────────────────────────────────────────────────────────────────

green()  { echo -e "\033[0;32m$*\033[0m"; }
yellow() { echo -e "\033[0;33m$*\033[0m"; }
red()    { echo -e "\033[0;31m$*\033[0m"; }
bold()   { echo -e "\033[1m$*\033[0m"; }

svc_status() {
    local name="$1"
    local state
    state=$(systemctl is-active "$name" 2>/dev/null || true)
    if [ "$state" = "active" ]; then
        green  "  ✔  $name: RUNNING"
    else
        local sub
        sub=$(systemctl show -p SubState --value "$name" 2>/dev/null || true)
        if [ "$sub" = "exited" ]; then
            green  "  ✔  $name: OK (oneshot completed)"
        else
            red    "  ✘  $name: STOPPED (state=$state)"
        fi
    fi
}

# Extract state/status from JSON — nodes use either "state" or "status" key
extract_state() {
    echo "$1" | grep -oE '"state":"[^"]*"|"status":"[^"]*"' | head -1 | cut -d'"' -f4
}

# ── MQTT live tail mode ───────────────────────────────────────────────────────

if [ "${1:-}" = "mqtt" ]; then
    echo "Subscribing to all winter-river topics (Ctrl+C to stop)..."
    mosquitto_sub -h "$BROKER" -p "$MQTT_PORT" -t "winter-river/#" -v
    exit 0
fi

# ── Full status report ────────────────────────────────────────────────────────

bold "========================================="
bold "  Winter River — System Status"
bold "========================================="
echo ""

# ── Services ──────────────────────────────────────────────────────────────────
bold "Services:"
svc_status winter-river-hotspot
svc_status mosquitto
svc_status postgresql
svc_status grafana-server

# NTP: Debian Trixie uses ntpsec; older installs use ntp/ntpd
if systemctl is-active --quiet ntpsec 2>/dev/null; then
    green "  ✔  ntpsec: RUNNING"
elif systemctl is-active --quiet ntp 2>/dev/null; then
    green "  ✔  ntp: RUNNING"
elif systemctl is-active --quiet ntpd 2>/dev/null; then
    green "  ✔  ntpd: RUNNING"
else
    red   "  ✘  ntp: STOPPED"
fi

# Docker stack (optional — only if docker is present)
if command -v docker &>/dev/null; then
    for container in influxdb telegraf; do
        if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${container}"; then
            green "  ✔  ${container} (docker): RUNNING"
        else
            yellow "  -  ${container} (docker): not running"
        fi
    done
fi
echo ""

# ── Hotspot ───────────────────────────────────────────────────────────────────
bold "Hotspot:"
if nmcli connection show --active 2>/dev/null | grep -q "$CON_NAME"; then
    IP=$(ip -4 addr show "$IFACE" 2>/dev/null | awk '/inet /{print $2}' | head -1)
    BAND=$(nmcli -g 802-11-wireless.band connection show "$CON_NAME" 2>/dev/null || echo "?")
    CHAN=$(nmcli -g 802-11-wireless.channel connection show "$CON_NAME" 2>/dev/null || echo "?")
    SSID=$(nmcli -g 802-11-wireless.ssid connection show "$CON_NAME" 2>/dev/null || echo "?")
    green  "  ✔  ACTIVE — SSID: $SSID"
    echo   "       IP: $IP   Band: $BAND   Channel: $CHAN"
else
    red    "  ✘  NOT running — start with: sudo ./scripts/setup_hotspot.sh"
fi
echo ""

# ── Connected clients (DHCP leases) ──────────────────────────────────────────
bold "Connected clients (DHCP):"
if [ -f /var/lib/misc/dnsmasq.leases ]; then
    COUNT=$(wc -l < /var/lib/misc/dnsmasq.leases)
    if [ "$COUNT" -gt 0 ]; then
        awk '{printf "  %-16s %s\n", $3, $4}' /var/lib/misc/dnsmasq.leases
    else
        yellow "  (none)"
    fi
else
    CLIENTS=$(arp -i "$IFACE" -n 2>/dev/null | grep -v "incomplete\|Address" || true)
    if [ -n "$CLIENTS" ]; then
        echo "$CLIENTS" | awk '{printf "  %-16s %s\n", $1, $3}'
    else
        yellow "  (none detected)"
    fi
fi
echo ""

# ── MQTT broker ping ──────────────────────────────────────────────────────────
bold "MQTT broker ($BROKER:$MQTT_PORT):"
if command -v mosquitto_pub &>/dev/null; then
    if timeout 3 mosquitto_pub -h 127.0.0.1 -p "$MQTT_PORT" \
        -t "winter-river/status-check" -m "ping" 2>/dev/null; then
        green "  ✔  Reachable"
    else
        red   "  ✘  Not reachable — check: sudo systemctl status mosquitto"
    fi
else
    yellow "  (mosquitto-clients not installed)"
fi
echo ""

# ── Node telemetry (retained MQTT) ────────────────────────────────────────────
bold "Node telemetry (retained MQTT):"
if ! command -v mosquitto_sub &>/dev/null; then
    yellow "  (mosquitto-clients not installed)"
else
    node_status() {
        local label="$1" node="$2"
        local MSG STATE
        MSG=$(timeout 2 mosquitto_sub -h 127.0.0.1 -p "$MQTT_PORT" \
            -t "winter-river/${node}/status" -C 1 2>/dev/null || true)
        if [ -n "$MSG" ]; then
            STATE=$(extract_state "$MSG")
            green  "  ✔  ${label}: ${STATE:-?}"
        else
            yellow "  -  ${label}: no data"
        fi
    }

    echo "  Side A:"
    node_status "utility_a        " utility_a
    node_status "mv_switchgear_a  " mv_switchgear_a
    node_status "mv_lv_transformer_a" mv_lv_transformer_a
    node_status "generator_a      " generator_a
    node_status "ats_a            " ats_a
    node_status "lv_dist_a        " lv_dist_a
    node_status "ups_a            " ups_a
    node_status "pdu_a            " pdu_a
    node_status "rectifier_a      " rectifier_a
    node_status "cooling_a        " cooling_a
    node_status "lighting_a       " lighting_a
    node_status "monitoring_a     " monitoring_a

    echo "  Side B:"
    node_status "utility_b        " utility_b
    node_status "mv_switchgear_b  " mv_switchgear_b
    node_status "mv_lv_transformer_b" mv_lv_transformer_b
    node_status "generator_b      " generator_b
    node_status "ats_b            " ats_b
    node_status "lv_dist_b        " lv_dist_b
    node_status "ups_b            " ups_b
    node_status "pdu_b            " pdu_b
    node_status "rectifier_b      " rectifier_b
    node_status "cooling_b        " cooling_b
    node_status "lighting_b       " lighting_b
    node_status "monitoring_b     " monitoring_b

    echo "  Shared:"
    node_status "server_rack      " server_rack
fi
echo ""

bold "========================================="
echo "  Tip: ./status.sh mqtt   — live MQTT feed"
echo "  Tip: sudo journalctl -u winter-river-hotspot -n 20"
echo "  Tip: sudo journalctl -u mosquitto -n 20"
echo "  Tip: sudo journalctl -u grafana-server -n 20"
bold "========================================="
