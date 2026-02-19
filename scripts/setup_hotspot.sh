#!/bin/bash
# Raspberry Pi WiFi Hotspot Setup Script
# Creates a local WiFi access point using NetworkManager
#
# Usage:
#   sudo ./setup_hotspot.sh          # Start hotspot
#   sudo ./setup_hotspot.sh stop     # Stop hotspot
#   sudo ./setup_hotspot.sh status   # Show hotspot status

set -euo pipefail

SSID="WinterRiver-AP"
PASSWORD="winterriver"
CON_NAME="winter-river-hotspot"
IFACE="wlan0"
IP_ADDR="192.168.4.1/24"

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo"
    exit 1
fi

check_interface() {
    if ! ip link show "$IFACE" &>/dev/null; then
        echo "ERROR: Interface $IFACE not found."
        echo "Available interfaces: $(ip link show | awk -F': ' '/^[0-9]/{print $2}' | tr '\n' ' ')"
        exit 1
    fi
}

start_hotspot() {
    check_interface
    echo "Setting up WiFi hotspot..."

    # First attempt: use nmcli's simpler hotspot helper (handles backend details)
    if nmcli device wifi hotspot ifname "$IFACE" ssid "$SSID" password "$PASSWORD"; then
        # Capture the active connection name for stop/remove actions
        NEW_CON_NAME=$(nmcli -t -f NAME,DEVICE connection show --active | awk -F: -v dev="$IFACE" '$2==dev{print $1; exit}') || true
        if [ -n "$NEW_CON_NAME" ]; then
            CON_NAME="$NEW_CON_NAME"
        fi
    else
        echo "nmcli hotspot helper failed, falling back to explicit connection profile..."
        # Remove existing connection with the same name if it exists
        nmcli connection delete "$CON_NAME" 2>/dev/null || true

        # Create the hotspot connection (fallback)
        nmcli connection add \
            type wifi \
            ifname "$IFACE" \
            con-name "$CON_NAME" \
            ssid "$SSID" \
            autoconnect no \
            wifi.mode ap \
            wifi.band bg \
            wifi.channel 6 \
            wifi-sec.key-mgmt wpa-psk \
            wifi-sec.psk "$PASSWORD" \
            ipv4.method shared \
            ipv4.addresses "$IP_ADDR"

        # Bring it up with visible error output
        if ! nmcli connection up "$CON_NAME"; then
            echo "ERROR: Failed to bring up hotspot. Check: journalctl -u NetworkManager -n 50"
            exit 1
        fi
    fi

    echo ""
    echo "========================================="
    echo "  Hotspot is running"
    echo "========================================="
    echo "  SSID:      $SSID"
    echo "  Password:  $PASSWORD"
    echo "  Gateway:   ${IP_ADDR%/*}"
    echo "  Interface: $IFACE"
    echo ""
    echo "  Clients get IPs via DHCP in 192.168.4.x range"
    echo "  Stop with: sudo $0 stop"
    echo "========================================="
}

stop_hotspot() {
    echo "Stopping hotspot..."
    nmcli connection down "$CON_NAME" 2>/dev/null || true
    nmcli connection delete "$CON_NAME" 2>/dev/null || true
    echo "Hotspot stopped."
}

show_status() {
    if nmcli connection show --active | grep -q "$CON_NAME"; then
        echo "Hotspot is ACTIVE"
        echo ""
        # Use device status for reliable output across NM versions
        nmcli -f GENERAL.STATE,IP4.ADDRESS,IP4.GATEWAY device show "$IFACE" 2>/dev/null || true
        echo ""
        echo "Connected clients:"
        # arp can miss clients; check dnsmasq leases if available
        if [ -f /var/lib/misc/dnsmasq.leases ]; then
            awk '{print "  "$3"\t"$4}' /var/lib/misc/dnsmasq.leases || echo "  (none)"
        else
            arp -i "$IFACE" -n 2>/dev/null || echo "  (none detected)"
        fi
    else
        echo "Hotspot is NOT running."
        echo "Start with: sudo $0"
    fi
}

case "${1:-start}" in
    start)   start_hotspot ;;
    stop)    stop_hotspot ;;
    status)  show_status ;;
    *)
        echo "Usage: sudo $0 [start|stop|status]"
        exit 1
        ;;
esac