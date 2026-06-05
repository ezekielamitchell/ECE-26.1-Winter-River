# Troubleshooting Guide

## ESP32 ADC Noise When WiFi Active

**Problem:** ADC readings fluctuate significantly (e.g., 2.3-2.6V when expecting 3.3V) when WiFi is enabled.

**Cause:** The ESP32's WiFi radio causes interference with ADC readings. This is a known hardware limitation.

**Solution - Software Averaging:**
```cpp
// Average multiple readings to reduce noise
long sum = 0;
for (int i = 0; i < 64; i++) {
  sum += analogRead(ADC_PIN);
}
int esp_raw_value = sum / 64;
double voltage = esp_raw_value * (3.3 / 4095.0);
```

**Additional Fixes:**
- Add a 0.1µF capacitor between the ADC pin and GND for hardware filtering
- Use ADC1 pins only (GPIO 32-39) - ADC2 pins do not work when WiFi is active
- ESP32 ADC is inherently ~1-2% inaccurate even with averaging

---

## SSD1306 OLED Not Displaying

**Problem:** The OLED stays blank, shows only noise, or never gets past the boot screen.

**Cause:** Most active nodes now use the shared `winter_river` helper, which initializes an SSD1306 OLED and probes I2C addresses `0x3C` then `0x3D`. Blank output usually means the module is not ACKing on either address, wiring is wrong, or the wrong display library was installed.

**Solution:**
1. Confirm the node is using `Adafruit SSD1306` and `Adafruit GFX` rather than `LiquidCrystal_I2C`
2. Check SDA/SCL wiring and power before debugging firmware
3. Watch serial boot output for the detected OLED address from `wr::begin()`
4. If needed, run a simple I2C scanner sketch to verify whether the panel responds at `0x3C` or `0x3D`

**platformio.ini:**
```ini
lib_deps =
    adafruit/Adafruit SSD1306@^2.5.7
    adafruit/Adafruit GFX Library@^1.11.5
```

**Historical note:** Older prototype docs may mention `LiquidCrystal_I2C` LCD modules. Those notes do not apply to the active 24-node SSD1306 firmware set.

---

## Why a Node Won't Connect (WiFi / MQTT)

**Problem:** A node's OLED shows `WiFi FAILED`, `WiFi LOST`, or `MQTT FAILED` and it reboots on a ~5–7 s cycle.

**First: read the cause off the screen.** The shared `winter_river` firmware no longer shows a bare "FAILED" — on the failure path it prints the specific reason to the OLED (and full detail to serial @115200). Decode it:

| On-screen | Meaning | Likely condition (below) |
|---|---|---|
| `WiFi FAILED` / `WiFi LOST` + `AP NOT SEEN` | Our SSID isn't on the air from this board's view | A2 (5 GHz/band), A3 (AP capacity), A7 (range/RF), A1 (AP not up yet) |
| `… + AP ok -NNdB` | AP is visible but the board still can't get a usable link | A5/A6 (auth), A8 (DHCP) — and the RSSI tells you signal quality |
| `… + scan: none` | Scan returned nothing (radio busy or air truly empty) | A1/A3 — retry; if persistent, AP side |
| `st=4 AUTH` | WPA handshake rejected | A5 (wrong PSK), A6 (security mismatch) |
| `st=1 NO AP` | Driver reports SSID unavailable | A2/A3/A7 |
| `MQTT FAILED` + `rc=-2 NO BROKER` | TCP to 192.168.4.1:1883 refused/unreachable | B1 (broker down), B7 (port blocked) |
| `rc=-4 TIMEOUT` | Connected TCP but no CONNACK | B1 (broker overloaded), B4 (socket wedge) |
| `rc=5 UNAUTH` | Broker rejected the client | B3 (`allow_anonymous false`) |
| `rc=2 BAD ID` | Client id rejected | B5 (duplicate `node_id` takeover) |
| `MQTT FAILED` + `WiFi -NNdB` | WiFi is up (RSSI shown), so it's purely the broker | any B |

### A. WiFi association failures (`WiFi FAILED` / `WiFi LOST`)
1. **AP not up yet** — the Pi boots in 30–60 s, an ESP32 in ~1 s. On a simultaneous cold start every node fails until the hotspot appears. **Power the Pi first.**
2. **AP on 5 GHz / wrong band** — ESP32 is 2.4 GHz only; if the hotspot isn't `band=bg ch6` it is invisible to every node.
3. **AP capacity ceiling** — the Pi's onboard radio reliably holds only ~8–10 stations in AP mode. With 24 nodes the station table saturates and associations get rejected/dropped. **Most likely cause when many nodes fail at once; not firmware-fixable.**
4. **WPA-handshake thundering herd** — many boards associating in the same instant overwhelm the AP (partly mitigated by the per-board connect stagger).
5. **SSID/password mismatch** — `SSID`/`PASSWORD` in `lib/winter_river/src/winter_river.h` must match `scripts/setup_hotspot.sh` exactly.
6. **Security mismatch** — nodes require WPA-PSK (`setMinSecurity(WIFI_AUTH_WPA_PSK)`); a WPA3/SAE-only AP is rejected.
7. **Weak signal / RF congestion** — low RSSI (distance, metal baseplate) or a busy channel 6.
8. **DHCP failure** — associates but never gets an IP (dnsmasq down, lease pool exhausted, packets lost). `WiFi.status()` never reaches `WL_CONNECTED`, so it looks like an association failure.
9. **Modem-sleep beacon misses** — mitigated by `WiFi.setSleep(false)` in `connectWifi()`.
10. **Power / brownout** — 24 boards' WiFi TX spikes can exceed a USB hub's budget → brownout reset. Hardware, not firmware.
11. **20 s connect timeout too short** under heavy AP load aborts a slow-but-valid association. Tunable: `WIFI_TIMEOUT_MS`.

### B. MQTT connection failures (`MQTT FAILED` — WiFi is already up)
1. **Broker down / unreachable** — mosquitto stopped or crashed (e.g. the `conf.d` duplicate-directive crash). `rc=-2`/`-4`.
2. **Broker bound to 127.0.0.1** — must be `listener 1883 0.0.0.0` (repo config is correct).
3. **`allow_anonymous false`** — nodes send no credentials → `rc=5` (repo config is correct).
4. **Socket wedge / keepalive timeout** — addressed by the non-blocking loop + QoS-0 control.
5. **Duplicate `node_id`** — two boards flashed with the same env continuously kick each other off (MQTT same-id takeover) → both flap. `rc=2`.
6. **Connect stalls 15 s** on an unreachable broker (`MQTT_SOCKET_TIMEOUT`) — slow recovery, not a hard failure.
7. **Port 1883 blocked** — firewall. Unlikely on a default Pi.

### C. Connects fine but looks dead (not a connect failure)
1. **Unknown `node_id`** — the broker drops telemetry from IDs not seeded in the `nodes` table ("Ignoring MQTT message from unknown node_id"). OLED shows `MQTT:OK` but nothing flows downstream. Re-seed via `scripts/init_db.sql`.
2. **Stale-node sweep** — a telemetry gap > 20 s (`STALE_NODE_THRESHOLD_SEC`) marks the node OFFLINE in the DB even while MQTT is alive.

### Pi-side diagnostics
```bash
./scripts/status.sh                                              # services + WiFi station count + DHCP + retained MQTT
iw dev wlan0 station dump | grep -c Station                      # raw associated-station count (capacity ceiling check)
nmcli -g 802-11-wireless.band connection show winter-river-hotspot  # must print 'bg' (2.4 GHz)
dmesg | grep -i brcmfmac | tail                                  # driver association rejects / "no space for new sta"
sudo journalctl -u mosquitto -n 50                               # broker health
mosquitto_sub -h 192.168.4.1 -t 'winter-river/#' -v              # who is actually publishing
```

**Rule of thumb:** if a handful of boards are rock-solid but it falls apart as you add more, that's the **AP capacity ceiling (A3)** — the fix is an external AP-capable WiFi adapter or a dedicated router, not more firmware.

---
