# Generator — `generator_a` / `generator_b`

## Real-World Role

Diesel (or natural gas) generators are the backbone of a data center's backup power system. When utility power fails, the generator must start, reach rated voltage and frequency, and accept the full facility load — all within 10–30 seconds (Tier III requires under 10 s transfer). Generators are typically rated 100 kW–5 MW per unit, with large hyperscale sites operating dozens of paralleled units for redundancy. Fuel capacity — typically 24–72 hours onsite — is a critical resilience metric that operators monitor continuously. The startup sequence (cranking, idle, governor-regulated speed, load acceptance) is modelled here by the simulation engine's `gen_timer` countdown.

---

## Nodes in This Topology

| node_id       | Side | Rated Voltage | Parent | Child   |
|---------------|------|---------------|--------|---------|
| `generator_a` | A    | 480 V         | none   | `ats_a` |
| `generator_b` | B    | 480 V         | none   | `ats_b` |

The generator is an autonomous power source. It connects to the ATS as the secondary (backup) input and is not downstream of any other node in normal operation.

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field      | Type   | Default  | Description                                    |
|------------|--------|----------|------------------------------------------------|
| `ts`       | string | HH:MM:SS | Local timestamp from NTP                       |
| `fuel_pct` | int    | 85       | Fuel tank level (%)                            |
| `rpm`      | int    | 0        | Engine RPM (0 = off / standby)                 |
| `output_v` | float  | 0.0      | Generator output voltage (V)                   |
| `load_pct` | int    | 0        | Output load as % of rated capacity             |
| `state`    | string | STANDBY  | Generator operational state (see States below) |
| `voltage`  | int    | 480      | Rated output voltage (V)                       |

---

## States

| State      | Meaning                                                                           |
|------------|-----------------------------------------------------------------------------------|
| `STANDBY`  | Utility is live; generator ready but engine not running                           |
| `STARTING` | Startup sequence in progress — cranking and ramping to governed speed             |
| `RUNNING`  | Engine at rated speed (> 1500 RPM), voltage stable, supplying load               |
| `FAULT`    | Fuel critical (< 5%) or engine running with RPM < 800 (stall / loss of governor) |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example           | Effect                                                              |
|------------------|-------------------|---------------------------------------------------------------------|
| `FUEL:<pct>`     | `FUEL:20`         | Sets fuel tank level (0–100); below 5% triggers `FAULT`            |
| `RPM:<rpm>`      | `RPM:1800`        | Sets engine RPM; drives `output_v` and state (see Auto-Thresholds) |
| `LOAD:<pct>`     | `LOAD:60`         | Sets output load percentage                                         |
| `STATUS:<state>` | `STATUS:RUNNING`  | Forces state string directly                                        |

---

## Auto-Thresholds

| Condition                       | Resulting State + Side Effect                    |
|---------------------------------|--------------------------------------------------|
| `fuel_pct` < 5                  | `FAULT`                                          |
| `rpm` = 0                       | `STANDBY`, `output_v` = 0.0 V                    |
| `rpm` > 0 and <= 1500           | `STARTING`, `output_v` = 0.0 V                   |
| `rpm` > 1500                    | `RUNNING`, `output_v` = 480.0 V                  |
| `RUNNING` with `rpm` < 800      | `FAULT` (stall detected)                         |

The simulation engine (`broker/main.py`) automatically commands the STARTING → RUNNING sequence by sending incremental `RPM` commands when upstream utility power is lost.

---

## Build & Flash

```bash
# Side A
pio run -e generator_a --target upload

# Side B
pio run -e generator_b --target upload

# Build only (no flash)
pio run -e generator_a
pio run -e generator_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/generator_a/status" -v

# Start the generator (simulates utility failure response)
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "RPM:1800"

# Simulate low fuel warning approaching fault
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "FUEL:3"

# Return to standby (utility restored)
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "RPM:0"
