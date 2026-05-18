# Rectifier — `rectifier` (shared, 2N)

## Real-World Role

High-voltage DC (HVDC) rectifiers convert 480V AC into 48V DC for direct
delivery to server power supplies, eliminating the AC-to-DC conversion stage
inside each individual server and reducing total conversion losses. Centralised
rectifier efficiency typically reaches 96–98% at optimal load (~50%), compared
to 89–92% for traditional server PSUs. Google, Meta, and many OCP-aligned
hyperscale operators have adopted 48V DC rack distribution for new server
designs.

In Winter River the rectifier is the **2N convergence point**: it accepts a
dual AC feed (`ups_a` primary, `ups_b` secondary) and delivers a single
48V DC bus to the shared `server_rack`. Both UPS feeds active → `NORMAL`;
either feed alone → `DEGRADED` (still delivering 48V DC); both feeds dead →
`OFF`.

---

## Nodes in This Topology

| node_id     | Side   | Input Voltage     | Output Voltage | Parents             | Child         |
|-------------|--------|-------------------|----------------|---------------------|---------------|
| `rectifier` | shared | 480 V AC (dual)   | 48 V DC        | `ups_a` + `ups_b`   | `server_rack` |

---

## Telemetry (published every 5s)

Topic: `winter-river/rectifier/status`

| Field          | Type   | Default  | Description                       |
|----------------|--------|----------|-----------------------------------|
| `ts`           | string | HH:MM:SS | Timestamp from NTP                |
| `input_v_ac`   | float  | 480.0    | AC input voltage (V)              |
| `output_v_dc`  | float  | 48.0     | DC output voltage (V)             |
| `load_pct`     | int    | 30       | Output load (%)                   |
| `path_a`       | int    | 1        | 1 = `ups_a` feed live, 0 = down   |
| `path_b`       | int    | 1        | 1 = `ups_b` feed live, 0 = down   |
| `state`        | string | NORMAL   | Rectifier operating state         |
| `ac_voltage`   | int    | 480      | Rated AC input voltage (V)        |
| `dc_voltage`   | int    | 48       | Rated DC output voltage (V)       |

---

## States

| State      | Meaning                                                       |
|------------|---------------------------------------------------------------|
| `NORMAL`   | Both UPS feeds live; DC output active within limits           |
| `DEGRADED` | One UPS feed lost; output still 48 V (running on single feed) |
| `FAULT`    | AC input overvoltage (> 528V) — output suppressed             |
| `OFF`      | Both AC feeds below threshold (< 400V) — no DC output         |

---

## MQTT Control Commands

Topic: `winter-river/rectifier/control`

Command payload from the broker:

```
INPUT_AC:<v> PATH_A:<0|1> PATH_B:<0|1> STATUS:<state>
```

| Token            | Example          | Effect                                              |
|------------------|------------------|-----------------------------------------------------|
| `INPUT_AC:<v>`   | `INPUT_AC:0`     | Set AC input; < 400V → OFF, > 528V → FAULT          |
| `PATH_A:<0|1>`   | `PATH_A:0`       | Report `ups_a` feed state on the OLED + telemetry   |
| `PATH_B:<0|1>`   | `PATH_B:1`       | Report `ups_b` feed state on the OLED + telemetry   |
| `LOAD:<pct>`     | `LOAD:60`        | Set output load percentage                          |
| `STATUS:<state>` | `STATUS:FAULT`   | Force state string                                  |

---

## Build & Flash

```bash
pio run -e rectifier --target upload

# Build only (no flash)
pio run -e rectifier
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/rectifier/status" -v

# Drop the Side-A UPS feed (rectifier should report DEGRADED but stay at 48V)
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:0"

# Drop both feeds (rectifier should drop to OFF; server_rack will FAULT)
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_b/control" -m "INPUT:0"
```
