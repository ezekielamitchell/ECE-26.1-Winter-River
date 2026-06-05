#!/bin/bash
# Raspberry Pi WiFi Hotspot Setup Script
# Creates a local WiFi access point using NetworkManager
#
# Usage:
#   sudo ./setup_hotspot.sh          # Start onboard wlan0 hotspot (default)
#   sudo ./setup_hotspot.sh external # External AP mode: serve DHCP/MQTT/NTP on wired eth
#   sudo ./setup_hotspot.sh stop     # Stop hotspot (onboard or external)
#   sudo ./setup_hotspot.sh status   # Show hotspot status
#
# External AP mode offloads the 2.4 GHz radio to a dedicated access point so the
# fleet can exceed the Pi onboard radio's ~10-13 simultaneous-station ceiling and
# bring all 24 boards online at once. See deploy/EXTERNAL_AP.md. Override the
# wired interface with WIRED_IFACE=<name> if yours isn't eth0.

set -euo pipefail

SSID="WinterRiver-AP"
PASSWORD="winterriver"
CON_NAME="winter-river-hotspot"
IFACE="wlan0"
IP_ADDR="192.168.4.1/24"

# External-AP mode: the Pi serves DHCP/MQTT/NTP over wired ethernet while a
# dedicated access point owns the radio. Override the iface if yours isn't eth0.
WIRED_IFACE="${WIRED_IFACE:-eth0}"
WIRED_CON="winter-river-wired"

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

# Remove the onboard wlan0 AP profile (idempotent).
teardown_onboard() {
    nmcli connection modify "$CON_NAME" autoconnect no 2>/dev/null || true
    nmcli connection down   "$CON_NAME" 2>/dev/null || true
    nmcli connection delete "$CON_NAME" 2>/dev/null || true
}

# Remove the external-mode wired DHCP profile (idempotent).
teardown_wired() {
    nmcli connection down   "$WIRED_CON" 2>/dev/null || true
    nmcli connection delete "$WIRED_CON" 2>/dev/null || true
}

# Print DHCP clients (shared by onboard + external status).
print_clients() {
    echo ""
    echo "Connected clients:"
    if [ -f /var/lib/misc/dnsmasq.leases ]; then
        awk '{print "  "$3"\t"$4}' /var/lib/misc/dnsmasq.leases || echo "  (none)"
    else
        arp -n 2>/dev/null || echo "  (none detected)"
    fi
}

# External AP mode: a dedicated access point owns the 2.4 GHz radio; the Pi
# serves DHCP/MQTT/NTP over wired ethernet at the same 192.168.4.1, so the
# fleet can exceed the onboard radio's station ceiling with no firmware change.
start_external() {
    if ! ip link show "$WIRED_IFACE" &>/dev/null; then
        echo "ERROR: wired interface '$WIRED_IFACE' not found."
        echo "Plug the Pi into the external AP's LAN/bridge port, then re-run."
        echo "If your wired iface isn't eth0: WIRED_IFACE=<name> sudo $0 external"
        echo "Available: $(ip -o link show | awk -F': ' '{print $2}' | tr '\n' ' ')"
        exit 1
    fi

    echo "Switching to EXTERNAL AP mode (DHCP/MQTT/NTP on $WIRED_IFACE)..."

    # 1. Drop the onboard radio AP so it isn't a second DHCP server on 192.168.4.1.
    teardown_onboard
    # Stop the boot unit from respawning the onboard AP on reboot. The wired
    # profile's autoconnect handles external-mode persistence on its own.
    systemctl disable winter-river-hotspot &>/dev/null || true

    # 2. Stand up the wired segment: 192.168.4.1/24 + dnsmasq DHCP (NM 'shared').
    teardown_wired
    nmcli connection add \
        type ethernet \
        ifname "$WIRED_IFACE" \
        con-name "$WIRED_CON" \
        autoconnect yes \
        ipv4.method shared \
        ipv4.addresses "$IP_ADDR" \
        ipv6.method ignore

    if ! nmcli connection up "$WIRED_CON"; then
        echo "ERROR: failed to bring up wired profile on $WIRED_IFACE."
        echo "Is the ethernet cable to the external AP plugged in?"
        echo "Check: journalctl -u NetworkManager -n 50"
        exit 1
    fi

    echo ""
    echo "========================================="
    echo "  External AP mode is active"
    echo "========================================="
    echo "  Pi (DHCP/MQTT/NTP): ${IP_ADDR%/*}  on $WIRED_IFACE"
    echo "  Configure the external AP as:"
    echo "    SSID $SSID / pass $PASSWORD / WPA2-PSK / 2.4 GHz / channel 6"
    echo "    DHCP OFF, AP/bridge mode, Pi on a LAN/bridge port"
    echo "  Nodes still reach the broker at ${IP_ADDR%/*} — no firmware change."
    echo "  Verify: ./scripts/status.sh"
    echo "========================================="
}

start_hotspot() {
    check_interface
    echo "Setting up onboard WiFi hotspot (2.4 GHz, channel 6)..."

    # If we were previously in external-AP mode, tear that down first so we don't
    # end up with two DHCP servers both claiming 192.168.4.1.
    teardown_wired
    systemctl enable winter-river-hotspot &>/dev/null || true

    # Tear down any leftover connection with the same name
    nmcli connection delete "$CON_NAME" 2>/dev/null || true

    # Build the connection profile directly so every parameter is explicit.
    # We do NOT use "nmcli device wifi hotspot" because that command does not
    # accept a band/channel argument on all NM versions, causing NetworkManager
    # to pick 5 GHz by default — which ESP32 hardware cannot connect to.
    nmcli connection add \
        type wifi \
        ifname "$IFACE" \
        con-name "$CON_NAME" \
        ssid "$SSID" \
        autoconnect yes \
        wifi.mode ap \
        wifi.band bg \
        wifi.channel 6 \
        wifi-sec.key-mgmt wpa-psk \
        wifi-sec.psk "$PASSWORD" \
        ipv4.method shared \
        ipv4.addresses "$IP_ADDR"

    if ! nmcli connection up "$CON_NAME"; then
        echo "ERROR: Failed to bring up hotspot."
        echo "Check: journalctl -u NetworkManager -n 50"
        exit 1
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
    echo "Stopping hotspot (onboard + external)..."
    # Disable autoconnect first so NM doesn't respawn either profile
    teardown_onboard
    teardown_wired
    # Bring wlan0 back to managed mode so normal WiFi can resume
    nmcli device set "$IFACE" managed yes 2>/dev/null || true
    echo "Hotspot stopped."
}

show_status() {
    if nmcli connection show --active 2>/dev/null | grep -q "$CON_NAME"; then
        echo "Hotspot is ACTIVE — mode: ONBOARD radio AP (wlan0)"
        echo ""
        nmcli -f GENERAL.STATE,IP4.ADDRESS,IP4.GATEWAY device show "$IFACE" 2>/dev/null || true
        echo ""
        # Show band/channel so we can confirm 2.4 GHz is in use
        echo -n "  Band/Channel: "
        nmcli -g 802-11-wireless.band,802-11-wireless.channel \
              connection show "$CON_NAME" 2>/dev/null \
              | tr '\n' '  ' || true
        echo ""
        print_clients
    elif nmcli connection show --active 2>/dev/null | grep -q "$WIRED_CON"; then
        echo "Hotspot is ACTIVE — mode: EXTERNAL AP (DHCP/MQTT/NTP on $WIRED_IFACE)"
        echo "  SSID/channel are configured on the external AP, not the Pi."
        echo ""
        nmcli -f GENERAL.STATE,IP4.ADDRESS device show "$WIRED_IFACE" 2>/dev/null || true
        print_clients
    else
        echo "Hotspot is NOT running (neither onboard nor external)."
        echo "  Onboard:  sudo $0"
        echo "  External: sudo $0 external   (see deploy/EXTERNAL_AP.md)"
    fi
}

case "${1:-start}" in
    start)    start_hotspot ;;
    external) start_external ;;
    stop)     stop_hotspot ;;
    status)   show_status ;;
    *)
        echo "Usage: sudo $0 [start|external|stop|status]"
        exit 1
        ;;
esac