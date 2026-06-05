# External Access Point Mode

Offload the WiFi radio to a dedicated 2.4 GHz access point so the full fleet can
connect at once. **No firmware changes** — nodes still reach the broker and NTP
at `192.168.4.1`.

## Why

The Raspberry Pi 5's onboard Broadcom radio (brcmfmac SoftAP) caps associated
stations at **roughly half the fleet (~10–13)**. With 24 boards racing to
associate, whichever side wins fills the slot table and **locks the other side
out**, and the last-to-associate boards (the server racks, at the bottom of each
side's boot chain) lose the race and reboot-loop. This is the hardware ceiling
called out in `CLAUDE.md` Key Design Decisions #7 and #15 — *"an external
AP/dongle is the next lever, not firmware."*

Confirm you're hitting it: `./scripts/status.sh` shows the **Associated WiFi
stations: Count** stuck around 10–13, never 24. Powering on only ~8 boards →
all connect; adding more → the surplus get rejected at the same threshold every
time (a clean count ceiling, not random RF dropouts).

The onboard cap lives in Broadcom's closed firmware — it **cannot** be raised
past the hardware limit via `hostapd`/`wpa_supplicant` `max_num_sta`. The fix is
more radio capacity.

## Topology (recommended)

A dedicated AP owns the radio; the Pi serves DHCP/MQTT/NTP over **wired
ethernet** at the same `192.168.4.1`. Every node IP/firmware assumption is
preserved.

```
  ESP32 ×24  ──2.4GHz──▶  External AP        ──ethernet──▶  Pi eth0
  WinterRiver-AP          (AP / bridge mode,                192.168.4.1/24
  ch6, WPA2-PSK           DHCP OFF, no NAT)                 dnsmasq DHCP
                                                            Mosquitto :1883
                                                            ntpsec, Docker
```

- Nodes associate to the **external AP**, which bridges them onto the wired
  segment.
- The Pi answers DHCP and hands out `192.168.4.x` leases (NetworkManager
  `shared` mode runs dnsmasq, same as the onboard hotspot did).
- Nodes reach MQTT (`192.168.4.1:1883`) and NTP (`192.168.4.1`) unchanged —
  both are hardcoded in `esp32-nodes/lib/winter_river/src/winter_river.h` and
  stay valid.
- `Mosquitto` already listens on `0.0.0.0:1883` and NTP is restricted to
  `192.168.4.0/24`, so neither needs a config change — eth0 is in that subnet.

## External AP configuration

Set the dedicated AP to match what the firmware expects:

| Setting   | Value                                                          |
|-----------|---------------------------------------------------------------|
| SSID      | `WinterRiver-AP`                                               |
| Password  | `winterriver`                                                 |
| Security  | **WPA2-PSK (AES/CCMP)** — not open, not WPA3-only, not Enterprise |
| Band      | **2.4 GHz only** (ESP32 has no 5 GHz radio)                   |
| Channel   | **6**                                                          |
| DHCP      | **OFF** — the Pi is the DHCP server                           |
| Mode      | **AP / bridge** (no router/NAT mode)                          |
| Pi cable  | Plug the Pi into a **LAN / bridge** port (not WAN)            |

Notes:
- The firmware sets `WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK)`, so WPA2-PSK is the
  sweet spot. Avoid WPA3-only and WPA/WPA2 "mixed" modes that force SAE.
- If the AP broadcasts one SSID across both bands, that's fine — the ESP32s only
  see the 2.4 GHz radio. Just make sure 2.4 GHz is enabled on channel 6.
- Pick an AP rated for ≥24 clients (most consumer APs and travel routers handle
  this easily; the Pi's onboard radio was the bottleneck, not the protocol).

## Switch the Pi to external mode

```bash
sudo ./scripts/setup_hotspot.sh external      # default wired iface: eth0
# If your wired interface isn't eth0:
WIRED_IFACE=end0 sudo ./scripts/setup_hotspot.sh external
```

This:
1. Tears down the onboard `winter-river-hotspot` (wlan0) profile so it isn't a
   second DHCP server on `192.168.4.1`.
2. Disables the boot-time `winter-river-hotspot.service` so it won't respawn the
   onboard AP and collide on reboot.
3. Creates the `winter-river-wired` profile on the wired interface:
   `192.168.4.1/24`, `ipv4.method shared` (dnsmasq DHCP). `autoconnect yes`
   makes it persist across reboots — no systemd unit needed.

Verify:

```bash
./scripts/status.sh
```

Expect the hotspot block to read **EXTERNAL AP (DHCP/MQTT/NTP on eth0)** and the
station/lease count to climb toward 24 as boards join — including all eight
`server_rack_*` nodes.

## Switch back to onboard mode

```bash
sudo ./scripts/setup_hotspot.sh start
```

Removes the wired profile, re-enables `winter-river-hotspot.service`, and brings
the wlan0 AP back up.

## Bonus: free the onboard radio as an uplink

In external mode the Pi's `wlan0` is unused, so you can point it at campus
WiFi/internet (real NTP source, Docker pulls, `apt`) while `eth0` serves the
node segment:

```bash
sudo nmcli device wifi connect "<campus-ssid>" password "<pw>" ifname wlan0
```

## Alternative topology (no spare ethernet)

If you can't wire the Pi to the AP, let a **travel router** do both DHCP and the
radio, and make the Pi a static client at `192.168.4.1`:

- Router LAN subnet `192.168.4.0/24`, router itself e.g. `192.168.4.254`.
- Reserve `192.168.4.1` for the Pi (DHCP reservation by the Pi's MAC, or set a
  static on the Pi).
- Nodes still hit MQTT/NTP at `192.168.4.1` (the Pi) — unchanged.

Trade-off: the **Pi is no longer the DHCP server**, so `status.sh`'s
"Connected clients (DHCP)" view reads the router's leases instead of the Pi's
and will show empty. Use the router's client list to count associations. This is
why the wired topology above is recommended.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Nodes associate but get no IP | AP's own DHCP is still on (turn it OFF), the Pi isn't on a bridged LAN port, or the `winter-river-wired` profile didn't come up — `./scripts/status.sh`. |
| Nodes never associate | AP is on 5 GHz, WPA3-only, or not channel 6. ESP32 is 2.4 GHz / WPA2-PSK only. |
| Two devices claim `192.168.4.1` | Onboard hotspot still up — `sudo ./scripts/setup_hotspot.sh external` tears it down; confirm with `nmcli connection show --active`. |
| `wired profile failed to bring up` | Ethernet cable to the AP isn't plugged in / no carrier. `journalctl -u NetworkManager -n 50`. |
| Onboard AP returns after reboot | The boot unit was re-enabled. Re-run `sudo ./scripts/setup_hotspot.sh external`. |
