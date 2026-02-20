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
    # is-active covers normal services; for Type=oneshot with RemainAfterExit
    # the state is "active" only if it completed successfully — check both
    local state
    state=$(systemctl is-active "$name" 2>/dev/null || true)
    if [ "$state" = "active" ]; then
        green  "  ✔  $name: RUNNING"
    else
        # For oneshot services, also accept if the unit completed (exited 0)
        local sub
        sub=$(systemctl show -p SubState --value "$name" 2>/dev/null || true)
        if [ "$sub" = "exited" ]; then
            green  "  ✔  $name: OK (oneshot completed)"
        else
            red    "  ✘  $name: STOPPED (state=$state)"
        fi
    fi
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

# Services
bold "Services:"
svc_status winter-river-hotspot
svc_status mosquitto
# Debian Trixie uses ntpsec instead of ntp/ntpd
if systemctl is-active --quiet ntpsec 2>/dev/null; then
    green "  ✔  ntpsec: RUNNING"
elif systemctl is-active --quiet ntp 2>/dev/null; then
    green "  ✔  ntp: RUNNING"
elif systemctl is-active --quiet ntpd 2>/dev/null; then
    green "  ✔  ntpd: RUNNING"
else
    red   "  ✘  ntp: STOPPED"
fi
echo ""

# Hotspot
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

# Connected clients (DHCP leases)
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

# MQTT broker ping — use 127.0.0.1 to avoid IPv6 resolution issues
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

# Last known node statuses (retained messages)
bold "Last node telemetry (retained MQTT):"
if command -v mosquitto_sub &>/dev/null; then
    # Chain order: util_a → trf_a → sw_a → gen_a → dist_a → ups_a → pdu_a → srv_a
    NODES=(util_a trf_a sw_a gen_a dist_a ups_a pdu_a srv_a)
    for node in "${NODES[@]}"; do
        MSG=$(timeout 2 mosquitto_sub -h 127.0.0.1 -p "$MQTT_PORT" \
            -t "winter-river/${node}/status" -C 1 2>/dev/null || echo "")
        if [ -n "$MSG" ]; then
            # Extract status field from JSON
            STATUS=$(echo "$MSG" | grep -o '"status":"[^"]*"' | cut -d'"' -f4 || echo "?")
            green  "  ✔  $node: $STATUS"
        else
            yellow "  -  $node: no data"
        fi
    done
else
    yellow "  (mosquitto-clients not installed)"
fi
echo ""

bold "========================================="
echo "  Tip: ./status.sh mqtt   — live MQTT feed"
echo "  Tip: sudo journalctl -u winter-river-hotspot -n 20"
echo "  Tip: sudo journalctl -u mosquitto -n 20"
bold "========================================="
