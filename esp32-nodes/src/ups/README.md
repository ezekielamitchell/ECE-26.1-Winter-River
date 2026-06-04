# Uninterruptible Power Supply (UPS) — `ups_a` / `ups_b`

## Real-World Role

The UPS is the critical bridge between utility or generator power and sensitive IT equipment. It conditions incoming AC, maintains battery energy storage, and provides seamless ride-through during the gap between utility failure and generator pickup — typically 10–30 seconds. Enterprise data centers use double-conversion (online) designs where all power flows through inverters continuously, eliminating any transfer time. Battery runtime targets are typically 10–20 minutes at full load to cover generator start, stabilisation, and a safety margin for extended outages.

---

## Nodes in This Topology

| node_id | Side | Rated Voltage | Parent            | Children                                                               |
|---------|------|---------------|-------------------|------------------------------------------------------------------------|
| `ups_a` | A    | 480 V AC      | `lv_switchgear_a` | `server_rack_a1`, `server_rack_a2`, `server_rack_a3`, `server_rack_a4` |
| `ups_b` | B    | 480 V AC      | `lv_switchgear_b` | `server_rack_b1`, `server_rack_b2`, `server_rack_b3`, `server_rack_b4` |

Each UPS is fed from its side's LV switchgear (the utility↔generator transfer
point) and single-feeds the 4 server_racks on its side. No shared rectifier —
sides are fully independent (block-redundant 2N). The racks handle their own
AC→DC conversion internally.

---

## Telemetry (published every 5s)

Topic: `winter-river/<node_id>/status`

| Field         | Type   | Default  | Description                        |
|---------------|--------|----------|------------------------------------|
| `ts`          | string | HH:MM:SS | Timestamp from NTP                 |
| `battery_pct` | int    | 100      | Battery state of charge (%)        |
| `load_pct`    | int    | 40       | Output load as % of rated capacity |
| `input_v`     | float  | 480.0    | AC input voltage (V)               |
| `output_v`    | float  | 480.0    | AC output voltage (V)              |
| `state`       | string | NORMAL   | UPS operating state                |
| `voltage`     | int    | 480      | Rated output voltage (V)           |

---

## States

| State        | Meaning                                                     |
|--------------|-------------------------------------------------------------|
| `NORMAL`     | AC input present, battery fully charged, output nominal     |
| `CHARGING`   | AC input present, battery recovering from a discharge event |
| `ON_BATTERY` | AC input lost — output sustained by battery discharge       |
| `FAULT`      | Battery depleted or input critically low — output at risk   |

---

## MQTT Control Commands

Topic: `winter-river/<node_id>/control`

| Command          | Example             | Effect                                               |
|------------------|---------------------|------------------------------------------------------|
| `BATT:<pct>`     | `BATT:15`           | Set battery state of charge (0–100)                  |
| `LOAD:<pct>`     | `LOAD:70`           | Set output load percentage                           |
| `INPUT:<v>`      | `INPUT:0`           | Set AC input voltage; triggers ON_BATTERY below 440V |
| `STATUS:<state>` | `STATUS:ON_BATTERY` | Force state string                                   |

---

## Auto-Thresholds

| Condition                                 | Resulting State |
|-------------------------------------------|-----------------|
| `battery_pct < 10` or `input_v < 400`    | `FAULT`         |
| `battery_pct < 25` or `input_v < 440`    | `ON_BATTERY`    |
| `input_v >= 440` and `battery_pct < 100` | `CHARGING`      |
| `input_v >= 440` and `battery_pct == 100`| `NORMAL`        |

> These auto-thresholds are a **standalone fallback**. When `broker/main.py` is
> driving the sim it sends an explicit `STATUS:` every tick and that takes
> precedence — the firmware applies the table above only when a control message
> carried no `STATUS:` token (the `status_set` gate). This stops the broker's
> `CHARGING` from being re-pinned to `ON_BATTERY` while the battery is still
> below 25 %.

---

## Build & Flash

```bash
# Side A
pio run -e ups_a --target upload

# Side B
pio run -e ups_b --target upload

# Build only (no flash)
pio run -e ups_a
pio run -e ups_b
```

---

## Quick Test

```bash
# Subscribe to live telemetry
mosquitto_sub -h 192.168.4.1 -t "winter-river/ups_a/status" -v

# Simulate utility input loss (triggers ON_BATTERY)
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:0"

# Restore input and set battery to charging level
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:480"
