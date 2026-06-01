# TESTING.md - Winter River System Verification and Scenario Runbook

Use this file as the canonical checklist for bringing up the Winter River rig,
verifying every layer, and running training scenarios for data center power,
cooling, observability, and failure response.

This runbook is intended for three moments:

- Pre-demo checkout before the rig is shown to students, judges, or sponsors.
- Guided lab sessions where trainees operate the simulator.
- Maintainer verification after firmware, broker, database, or dashboard changes.

Related references:

- [Deployment guide](docs/deployment.md)
- [Communication protocols](docs/communication-protocols.md)
- [2N redundancy guide](docs/2n-redundancy.md)
- [ESP32 troubleshooting](esp32-nodes/TROUBLESHOOTING.md)

## Operating Rules

- Mark a checkbox complete only when the behavior is visible.
- For power-path checks, verify at least two evidence sources:
  - ESP32 OLED display.
  - MQTT telemetry with `./scripts/status.sh mqtt` or `mosquitto_sub`.
  - Broker log or simulation output.
  - Grafana or InfluxDB data.
- Run commands from the repository root on the Raspberry Pi unless noted.
- When `broker/main.py` is running, it may overwrite manual `/control` commands
  on the next simulation tick. For cascade tests, prefer faulting upstream nodes
  and letting the broker compute downstream state.
- Before a public demo, rehearse the exact scenarios and recovery commands that
  will be shown.

## Run Record

Copy this block into lab notes for every session.

```text
Date:
Location:
Demo lead / instructor:
Recorder:
Git commit:
Pi hostname:
Pi IP address:
WiFi SSID:
Broker/database topology used:
Physical nodes installed:
Baseplate slots populated (of 24):
Boards exceeding baseplate (26 active vs 24 slots — 2-slot overflow):
Firmware environments flashed:
Grafana dashboard used:
Known missing or skipped modules:
Scenarios run:
Pass / needs work:
Notes:
```

Helpful commands:

```bash
git rev-parse --short HEAD
hostname
ip -4 addr show wlan0
./scripts/status.sh
```

## Topology Under Test

The current broker/database simulation topology is **block-redundant 2N** —
two fully independent power chains, each feeding 4 single-fed server racks.
There is no shared rectifier and no rack-level 2N: side-A failure kills all
4 of side-A's racks, and side-B continues unaffected.

```text
Side A:
utility_a -> hv_mv_transformer_a -> mv_switchgear_a -> mv_lv_transformer_a
          -> lv_switchgear_a -> ats_a
generator_a ---------------------------------------------------^ (ats secondary)
ats_a -> ups_a -> server_rack_a1, _a2, _a3, _a4
ats_a -> cooling_a   (mech load, parallel to ups_a)

Side B (mirror):
utility_b -> hv_mv_transformer_b -> mv_switchgear_b -> mv_lv_transformer_b
          -> lv_switchgear_b -> ats_b
generator_b ---------------------------------------------------^ (ats secondary)
ats_b -> ups_b -> server_rack_b1, _b2, _b3, _b4
ats_b -> cooling_b

bms (broker-synthesized) -- broker/main.py reads live node state every tick
                         -> publishes rolled-up winter-river/bms/status
```

Each server rack is **single-fed** from its side's UPS. There is no PATH_A /
PATH_B at the rack level any more — the broker's BMS aggregator derives
`2N_HEALTHY | A_ONLY | B_ONLY | DOWN` from per-side UPS health and rolls up
worst-of rack states across each side's 4 racks.

Active node IDs (26 broker/DB nodes + 1 broker-synthesized BMS topic):

| Group | Node IDs |
|---|---|
| Side A (13) | `utility_a`, `hv_mv_transformer_a`, `mv_switchgear_a`, `mv_lv_transformer_a`, `lv_switchgear_a`, `generator_a`, `ats_a`, `ups_a`, `cooling_a`, `server_rack_a1`, `server_rack_a2`, `server_rack_a3`, `server_rack_a4` |
| Side B (13) | `utility_b`, `hv_mv_transformer_b`, `mv_switchgear_b`, `mv_lv_transformer_b`, `lv_switchgear_b`, `generator_b`, `ats_b`, `ups_b`, `cooling_b`, `server_rack_b1`, `server_rack_b2`, `server_rack_b3`, `server_rack_b4` |
| Broker-synthesized (no ESP32 row) | `bms`, `facility`, `weather` — broker publishes each retained from live state every tick |

⚠ **Slot budget overflow:** 26 active boards vs 24 baseplate slots = 2-slot
overflow. Resolve by (a) expanding the baseplate, (b) retiring one rack per
side (drops to 24, both sides still mirrored at 12 each), or (c) running the
2 overflow boards on a separate carrier. Document the chosen resolution in
the Run Record.

DB-row accounting: `nodes` table holds **26 rows** (13 Side A + 13 Side B; no
shared rows). `bms`, `facility`, and `weather` are intentionally absent from
`nodes` — they are broker-published synthetic topics. The broker's
`online_nodes` ceiling is **26**.

Unknown-node guard:

- If a node publishes telemetry but is not present in the `nodes` table, the
  broker logs "Ignoring MQTT message from unknown node_id" and drops the
  telemetry instead of failing — re-flash the board with a current env.

## Part 1 - Pre-Flight Checklist

Run this every time the rig is unboxed, repowered, moved, reflashed, or shown in
front of an audience.

### 1.1 Physical Setup

- [ ] Raspberry Pi 5 is powered from a stable supply.
- [ ] Pi microSD card is inserted and seated.
- [ ] PCB baseplate or breadboard harness is not shorted or mechanically loose.
- [ ] All expected ESP32 modules are seated in the intended positions.
- [ ] USB-C or power leads are fully inserted.
- [ ] No connector is scorched, cracked, or hot.
- [ ] All SSD1306 OLEDs are visible and undamaged.
- [ ] Physical labels match MQTT node IDs.
- [ ] Side A and Side B are visually separated.
- [ ] All 4 server racks per side are clearly marked as the IT loads, with
      side-A racks (`server_rack_a1..a4`) and side-B racks (`server_rack_b1..b4`)
      grouped together.
- [ ] Workspace ambient temperature is logged: ______ deg F.
- [ ] Spare USB cable, laptop, and serial monitor are available.
- [ ] Baseplate slot accounting: 24 slots available / 26 boards defined in
      firmware. Record the 2-slot overflow resolution (expand / drop 2 racks /
      external carrier) in the Run Record before continuing.

### 1.2 Node Inventory

The active rig defines **26 ESP32 boards** — 13 Side A + 13 Side B, no shared
boards. The baseplate has 24 slots, so the overflow is handled per the Run
Record (typically: retire one server_rack per side to fit exactly, leaving
12 per side / 24 total).

All 26 boards are seeded in `nodes` (broker validates telemetry against this
table); `bms`, `facility`, and `weather` are broker-synthesized retained
topics and have no DB rows.

Physical board inventory (26 boards):

| #  | node_id                | Powered | OLED | WiFi | MQTT | Notes |
|----|------------------------|---------|------|------|------|-------|
|  1 | `utility_a`            | [ ]     | [ ]  | [ ]  | [ ]  | 230 kV grid feed |
|  2 | `hv_mv_transformer_a`  | [ ]     | [ ]  | [ ]  | [ ]  | 230 kV → 34.5 kV |
|  3 | `mv_switchgear_a`      | [ ]     | [ ]  | [ ]  | [ ]  | 34.5 kV MV-bus breaker |
|  4 | `mv_lv_transformer_a`  | [ ]     | [ ]  | [ ]  | [ ]  | 34.5 kV → 480 V |
|  5 | `lv_switchgear_a`      | [ ]     | [ ]  | [ ]  | [ ]  | 480 V LV-bus breaker |
|  6 | `generator_a`          | [ ]     | [ ]  | [ ]  | [ ]  | |
|  7 | `ats_a`                | [ ]     | [ ]  | [ ]  | [ ]  | LV transfer switch |
|  8 | `ups_a`                | [ ]     | [ ]  | [ ]  | [ ]  | feeds all 4 side-A racks |
|  9 | `cooling_a`            | [ ]     | [ ]  | [ ]  | [ ]  | 55 fans (mech load off ats_a) |
| 10 | `server_rack_a1`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_a |
| 11 | `server_rack_a2`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_a |
| 12 | `server_rack_a3`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_a |
| 13 | `server_rack_a4`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_a |
| 14 | `utility_b`            | [ ]     | [ ]  | [ ]  | [ ]  | 230 kV grid feed |
| 15 | `hv_mv_transformer_b`  | [ ]     | [ ]  | [ ]  | [ ]  | 230 kV → 34.5 kV |
| 16 | `mv_switchgear_b`      | [ ]     | [ ]  | [ ]  | [ ]  | 34.5 kV MV-bus breaker |
| 17 | `mv_lv_transformer_b`  | [ ]     | [ ]  | [ ]  | [ ]  | 34.5 kV → 480 V |
| 18 | `lv_switchgear_b`      | [ ]     | [ ]  | [ ]  | [ ]  | 480 V LV-bus breaker |
| 19 | `generator_b`          | [ ]     | [ ]  | [ ]  | [ ]  | |
| 20 | `ats_b`                | [ ]     | [ ]  | [ ]  | [ ]  | LV transfer switch |
| 21 | `ups_b`                | [ ]     | [ ]  | [ ]  | [ ]  | feeds all 4 side-B racks |
| 22 | `cooling_b`            | [ ]     | [ ]  | [ ]  | [ ]  | 55 fans (mech load off ats_b) |
| 23 | `server_rack_b1`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_b |
| 24 | `server_rack_b2`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_b |
| 25 | `server_rack_b3`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_b |
| 26 | `server_rack_b4`       | [ ]     | [ ]  | [ ]  | [ ]  | single-fed from ups_b |
|    | `bms` (optional)       | [ ]     | [ ]  | [ ]  | [ ]  | OLED mirror only; broker-synthesized topic, off-baseplate carrier |

### 1.3 Raspberry Pi Services

- [ ] `./scripts/status.sh` returns no unexpected `STOPPED` lines.
- [ ] `winter-river-hotspot` is active or completed successfully.
- [ ] `mosquitto` is running.
- [ ] `ntpsec`, `ntp`, or `ntpd` is running.
- [ ] `postgresql` is running if the simulation engine uses the database.
- [ ] `influxdb` is running if dashboards use InfluxDB.
- [ ] `telegraf` is running if MQTT telemetry is ingested.
- [ ] `grafana-server` or the Docker Grafana container is running.
- [ ] No service logs show repeated restart loops.

Commands:

```bash
./scripts/status.sh
systemctl status winter-river-hotspot mosquitto postgresql
systemctl status influxdb telegraf grafana-server
journalctl -u mosquitto -n 50
```

If using Docker for the monitoring stack:

```bash
cd grafana
docker compose ps
docker compose logs --tail=50 telegraf
```

### 1.4 WiFi Hotspot

- [ ] SSID is `WinterRiver-AP`.
- [ ] Pi hotspot IP is `192.168.4.1/24`.
- [ ] WiFi band is `bg` or otherwise confirmed as 2.4 GHz.
- [ ] WiFi channel is 6.
- [ ] A phone or laptop can join the hotspot.
- [ ] DHCP leases appear for connected ESP32 nodes.
- [ ] ESP32 nodes reconnect automatically after Pi reboot.

Commands:

```bash
sudo ./scripts/setup_hotspot.sh status
nmcli connection show --active
nmcli -g 802-11-wireless.band connection show winter-river-hotspot
nmcli -g 802-11-wireless.channel connection show winter-river-hotspot
ip -4 addr show wlan0
cat /var/lib/misc/dnsmasq.leases
```

Pass condition: every expected ESP32 appears as a DHCP client or publishes MQTT
telemetry within 60 seconds of power-up.

### 1.5 MQTT Broker

- [ ] Broker accepts local publishes on port 1883.
- [ ] Broker accepts publishes through `192.168.4.1`.
- [ ] `winter-river/#` subscription shows live telemetry.
- [ ] Each online node has a retained `winter-river/<node_id>/status` message.
- [ ] No retained `OFFLINE` message remains for a node that should be online.
- [ ] Node `/status` messages are retained.
- [ ] Node `/control` messages are not retained.
- [ ] Last Will and Testament publishes `OFFLINE` when a node is unplugged.
- [ ] Reconnecting a node overwrites retained `OFFLINE` with `ONLINE` or current
      telemetry.

Commands:

```bash
mosquitto_pub -h 127.0.0.1 -t "winter-river/status-check" -m "ping"
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
./scripts/status.sh mqtt
```

Clear a stale retained message only after confirming the node really should not
be offline:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/<node_id>/status" -r -n
```

### 1.6 ESP32 Firmware And OLEDs

For each installed node:

- [ ] Firmware environment name matches the physical label.
- [ ] Serial monitor boots without repeated resets.
- [ ] OLED initializes before WiFi.
- [ ] OLED address probe detects `0x3C` or `0x3D`.
- [ ] Node connects to `WinterRiver-AP`.
- [ ] Node syncs time from the Pi NTP server.
- [ ] Node publishes telemetry every 5 seconds.
- [ ] Node subscribes to `winter-river/<node_id>/control`.
- [ ] Node responds to at least one manual control command.
- [ ] OLED state matches latest MQTT state.

Build checks from a development machine:

```bash
cd esp32-nodes
pio run -e utility_a
pio run -e mv_switchgear_a
pio run -e generator_a
pio run -e ats_a
pio run -e ups_a
pio run -e server_rack_a1
pio run -e bms
```

Full firmware matrix check:

```bash
cd esp32-nodes
pio run
```

### 1.7 Simulation Engine And Database

- [ ] PostgreSQL is running.
- [ ] `scripts/init_db.sql` has been applied to the intended database.
- [ ] `nodes` contains the expected active topology for this demo.
- [ ] `live_status` has one row for every active node.
- [ ] `facility_metrics` exists, or the broker logs the one-time disable warning
      and continues.
- [ ] Unknown MQTT node IDs do not crash the broker.
- [ ] `broker/main.py` connects to MQTT.
- [ ] `broker/main.py` connects to PostgreSQL.
- [ ] Simulation tick rate is about 1 second.
- [ ] Control commands are published on every tick.
- [ ] Cascading failure logic follows parent-child topology.

Commands:

```bash
psql -U postgres -d sensor_data -f scripts/init_db.sql
psql -U postgres -d sensor_data -c \
  "SELECT node_id,node_type,parent_id,secondary_parent_id FROM nodes ORDER BY node_id;"

cd broker
source venv/bin/activate
python main.py
```

### 1.8 Monitoring Stack

- [ ] Grafana opens at `http://192.168.4.1:3000`.
- [ ] InfluxDB opens or responds at `http://192.168.4.1:8086`.
- [ ] Telegraf subscribes to `winter-river/+/status`.
- [ ] New MQTT telemetry appears in InfluxDB within about 10 seconds.
- [ ] Grafana dashboard panels update without manual refresh.
- [ ] Dashboard distinguishes Side A, Side B, and shared load.
- [ ] Dashboard shows online/offline, state, voltage, load, battery, temperature,
      airflow, and PUE where available.
- [ ] A scenario fault is visible in Grafana and on the physical OLED.

Commands:

```bash
journalctl -u telegraf -n 50
journalctl -u grafana-server -n 50
influx ping
```

Example InfluxDB query:

```bash
influx query '
  from(bucket: "mqtt_metrics")
    |> range(start: -5m)
    |> limit(n: 20)
' --org iot-project --token "$INFLUXDB_ADMIN_TOKEN"
```

### 1.9 Baseline Normal State

Complete this before running failure scenarios.

- [ ] Both utilities show `GRID_OK`.
- [ ] Both MV switchgear nodes show `CLOSED`.
- [ ] Both HV/MV transformers show normal pass-through.
- [ ] Both LV switchgear nodes show closed/normal.
- [ ] Both MV/LV transformers show normal temperature and 480 V output.
- [ ] Both generators show `STANDBY`, not `RUNNING`.
- [ ] Both ATS nodes use `UTILITY`.
- [ ] Both UPS nodes show `NORMAL`, 480 V input, 480 V output, and healthy
      battery.
- [ ] All 4 `server_rack_a{1..4}` show `NORMAL` (single-fed from `ups_a`).
- [ ] All 4 `server_rack_b{1..4}` show `NORMAL` (single-fed from `ups_b`).
- [ ] Cooling is normal on both sides.
- [ ] `bms` shows `mode:NORMAL`, `power_state:2N_HEALTHY`, `redundancy_lost:0`,
      `active_alarms:0`, `online_nodes:26` (all 26 broker/DB nodes online;
      `bms`/`facility`/`weather` are broker-synthesized and not counted).
- [ ] Grafana shows no stale fault state from a previous run.

Suggested baseline commands:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_b/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_b/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "STATUS:NORMAL TEMP:108 LOAD:45"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_b/control" -m "STATUS:NORMAL TEMP:108 LOAD:45"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 LOAD:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:STANDBY RPM:0 LOAD:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_a/control" -m "SOURCE:UTILITY STATUS:UTILITY"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_b/control" -m "SOURCE:UTILITY STATUS:UTILITY"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_b/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
# Each of the 8 server racks gets the same NORMAL/INPUT baseline; broker
# fills in TEMP every tick from the thermal model.
for r in a1 a2 a3 a4 b1 b2 b3 b4; do
    mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_${r}/control" -m "INPUT:480 STATUS:NORMAL"
done
```

Note: `bms` is broker-driven — `winter-river/bms/status` is published by
`broker/main.py` every tick from live node state. The broker does not
subscribe to `winter-river/bms/control`, so manual publishes to that topic
are no-ops; the way to "reset" the BMS rollup is to clear the upstream
faults and let the next tick recompute. If you need to wipe a stale retained
`bms/status` while the broker is stopped, clear it directly:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/bms/status" -r -n
```

## Part 2 - Layer-By-Layer Smoke Tests

Run these after pre-flight and before scripted scenarios. These isolate one
layer at a time so failures can be traced quickly.

### 2.1 MQTT Round Trip

```bash
# Terminal 1
mosquitto_sub -h 192.168.4.1 -t "winter-river/utility_a/control" -v

# Terminal 2
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK"
```

- [ ] Subscriber prints the published message within about 100 ms.

### 2.2 LWT Disconnect Detection

- [ ] Subscribe to `winter-river/cooling_a/status`.
- [ ] Unplug `cooling_a`.
- [ ] Broker publishes retained `{"node":"cooling_a","status":"OFFLINE"}`.
- [ ] Reconnect `cooling_a`.
- [ ] Node publishes `ONLINE` or normal telemetry.
- [ ] Repeat with a critical node such as `utility_a` only if cascade testing is
      approved.

Command:

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/cooling_a/status" -v
```

### 2.3 OLED Address Probe

- [ ] Serial monitor shows OLED init succeeded.
- [ ] Display works on modules using `0x3C`.
- [ ] Display works on modules using `0x3D`.
- [ ] No node boots with a blank OLED while serial reports OLED failure.

### 2.4 NTP Sync

- [ ] Serial monitor on any node reports NTP sync.
- [ ] Telemetry `ts` field matches wall clock within about 2 seconds.
- [ ] OLED timestamp or state does not freeze after WiFi reconnect.

### 2.5 Control Token Parser

Send a compound command:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" \
  -m "RPM:1800 STATUS:RUNNING LOAD:60"
```

- [ ] `generator_a` OLED reflects all changed fields, not only the first token.

### 2.6 Simulation Propagation

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_sub -h 192.168.4.1 -t "winter-river/+/control" -v
```

- [ ] Broker publishes downstream Side A commands within 1-2 ticks.
- [ ] Side B commands remain normal.
- [ ] Restore utility before continuing.

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

### 2.7 Unknown Node Guard

Use only if it is safe to publish a test message.

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/not_a_real_node/status" -m '{"state":"NORMAL"}'
```

- [ ] Broker logs that the node ID is unknown.
- [ ] Broker does not crash.
- [ ] No invalid row is inserted into `historical_data`.

### 2.8 Thermal Model Baseline

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/facility/status" -C 1 -v
```

- [ ] Facility status is retained.
- [ ] `mode` is `NORMAL` at baseline.
- [ ] `cold_aisle_f` is in a sane range for the weather preset.
- [ ] `fan_count` matches the expected active fan count.
- [ ] `pue` is finite and plausible.

### 2.9 BMS Aggregator Smoke Test

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/bms/status" -C 1 -v
```

- [ ] `bms/status` is retained.
- [ ] At baseline, `mode:NORMAL`, `power_state:2N_HEALTHY`,
      `redundancy_lost:0`, `degraded_paths:0`, `active_alarms:0`.
- [ ] `online_nodes` equals the count of DB-seeded active nodes — **26** under
      the current topology (`bms`, `facility`, and `weather` are broker-synthesized
      and not in `nodes`, so they do not count toward `online_nodes`); less if any
      node is missing.
- [ ] Side A and Side B health flags match the actual `_a` / `_b` chain states.

Drive one upstream fault and confirm the BMS reflects it within ~2 ticks:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
sleep 3
mosquitto_sub -h 192.168.4.1 -t "winter-river/bms/status" -C 1 -v
```

- [ ] `bms/status` now reflects the Side A degradation (mode/state changed,
      `degraded_paths` incremented, `active_alarms` > 0).

Restore before continuing:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

### 2.10 Grafana Pipeline

- [ ] Publish or wait for a fresh node telemetry point.
- [ ] Telegraf logs no parse or connection errors.
- [ ] InfluxDB receives the point.
- [ ] Grafana panel updates within about 10 seconds.

## Part 3 - Scenario Runbook

Each scenario has setup, trigger, expected result, recovery, and learning
questions. Keep this live observer running during scenario tests:

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
```

### Scenario 0 - Normal Walkdown

Objective: teach the physical topology and prove the system starts healthy.

Setup:

- [ ] Complete Part 1.
- [ ] Confirm both paths are normal.

Steps:

- [ ] Trainee traces Side A from `utility_a` → `hv_mv_transformer_a` →
      `mv_switchgear_a` → `mv_lv_transformer_a` → `lv_switchgear_a` →
      `ats_a` → `ups_a` → `server_rack_a{1..4}`, and notes the parallel
      mech branch `ats_a → cooling_a`.
- [ ] Trainee traces Side B end-to-end and confirms it's a complete mirror
      of Side A — no shared nodes anywhere.
- [ ] Trainee identifies utilities, MV switchgear, generators, ATS units,
      UPS units, cooling (both sides), all 8 server racks, and the `bms`
      aggregator (broker-published, no ESP32 row in `nodes`).
- [ ] Trainee reads `winter-river/bms/status` and confirms
      `power_state:2N_HEALTHY`, `rack_a_state:NORMAL`, `rack_b_state:NORMAL`
      matches what the individual node OLEDs show.
- [ ] Operator compares OLEDs, MQTT, and Grafana.

Expected:

- [ ] Both utility paths are available.
- [ ] Both MV switchgear nodes report `CLOSED`.
- [ ] Generators are in `STANDBY`.
- [ ] Both UPS nodes report `NORMAL`, 480 V in/out.
- [ ] All 8 server racks report `NORMAL`.
- [ ] Grafana and MQTT agree with OLED state.

Questions:

- Which nodes are sources, distribution equipment, and loads?
- Why should the generator normally be in standby?
- Which nodes are duplicated, and which nodes are shared?

### Scenario 1 - Utility Outage With Generator Startup

Objective: show utility loss, generator startup delay, ATS transfer, and UPS
ride-through.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
```

Expected:

- [ ] `utility_a` reports `OUTAGE`.
- [ ] `generator_a` transitions `STANDBY` -> `STARTING` -> `RUNNING`.
- [ ] `ats_a` transfers from `UTILITY` to `GENERATOR`.
- [ ] `ups_a` carries load during the startup window (state `ON_BATTERY` then
      `CHARGING` once gen comes up).
- [ ] Side B remains normal throughout.
- [ ] Server rack stays `NORMAL` once the gen has picked up Side A. It may
      momentarily flag `DEGRADED` only during the transfer window itself; if it
      remains `DEGRADED` after the gen reports `RUNNING`, the ATS transfer or
      generator pickup failed and the test has not passed.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

- [ ] ATS returns to `UTILITY`.
- [ ] Generator returns to `STANDBY`.
- [ ] Server rack returns to `NORMAL`.

Questions:

- Why does generator power not appear instantly?
- What bridges the transfer gap?
- Why should Side B be unaffected?

### Scenario 2 - Utility Sag Or Frequency Abnormality

Objective: distinguish poor-quality utility power from total utility loss.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:SAG VOLT:184.0 FREQ:58.8"
```

Expected:

- [ ] `utility_a` displays abnormal voltage and frequency.
- [ ] Downstream behavior follows the broker model for an energized but abnormal
      utility condition.
- [ ] Trainee can explain whether the condition is warning-level or transfer-
      worthy.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- How is a sag different from an outage?
- Which equipment is sensitive to voltage or frequency quality?
- What measurements prove utility power is healthy again?

### Scenario 3 - Generator Fails During Utility Outage

Objective: show risk when the standby source is unavailable.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
```

Expected:

- [ ] ATS cannot transfer to a usable generator source.
- [ ] Side A downstream equipment loses normal source power.
- [ ] UPS carries load only as long as battery behavior allows.
- [ ] Server rack remains online only if Side B is healthy.
- [ ] System is `DEGRADED`, not comfortable, even if IT load remains up.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- What should a technician verify before blaming the ATS?
- What alarms are urgent versus informational?
- What work should stop while only one side is healthy?

### Scenario 4 - Full Side Failure, Block-Redundant 2N Degraded Operation

Objective: prove that one full side can fail without dropping the other side's
4 server racks. In block-redundant 2N, side-A failure kills all 4 side-A racks
at once — there is no rack-level dual feed. Side B continues normally.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
```

Expected:

- [ ] Side A cannot energise `ups_a` (utility out, generator failed).
- [ ] `ups_a` runs on battery briefly, then reports `FAULT` once depleted.
- [ ] All 4 `server_rack_a{1..4}` cascade to `FAULT` (single-fed from `ups_a`).
- [ ] Side B stays normal: `ups_b` NORMAL, all 4 `server_rack_b{1..4}` NORMAL.
- [ ] `bms/status` shows `mode:ALARM`, `power_state:B_ONLY`,
      `redundancy_lost:1`, `side_a_health:DOWN`, `side_b_health:OK`,
      `rack_a_state:FAULT`, `rack_b_state:NORMAL`.
      (Mode is `ALARM` rather than `DEGRADED` because the Side A cascade
      puts multiple nodes into fault states, pushing `active_alarms` above 1.
      The broker's `_compute_bms` promotes `DEGRADED` → `ALARM` once
      `active_alarms > 1`.)
- [ ] Operators can explain that half the IT capacity is gone (4 of 8 racks)
      and recovery requires either utility restoration or generator repair —
      a second fault on Side B now drops the remaining 4 racks too.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- What does "block-redundant 2N" mean in practical terms, and how does it
  differ from rack-level 2N?
- Why does losing one side drop 4 racks instead of being absorbed at the rack
  level (as in a true dual-fed-per-rack design)?
- What's the operational cost of block redundancy vs rack-level 2N?

### Scenario 4b - Single Rack Failure (Workload-Level Redundancy)

Objective: show that losing one rack is not the same as losing a side —
the 3 other racks on the same side, plus all 4 on the other side, keep
running. Workloads should fail over via cluster software.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_a1/control" -m "STATUS:FAULT"
```

Expected:

- [ ] `server_rack_a1` reports `FAULT`.
- [ ] `server_rack_a2`, `_a3`, `_a4` remain `NORMAL` (same UPS, same side).
- [ ] All 4 `server_rack_b{1..4}` remain `NORMAL`.
- [ ] `bms/status` shows `mode:DEGRADED`, `power_state:2N_HEALTHY` (power is
      fine — this is a rack-internal fault), `rack_a_state:FAULT` (worst-of
      across the 4 a-racks), `rack_b_state:NORMAL`, `active_alarms:>=1`.
- [ ] If you're running real workloads, trainees should articulate that
      VMs/containers should have already failed over to one of the other 7
      racks — this is workload-level redundancy, distinct from
      power-level redundancy.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_a1/control" -m "STATUS:NORMAL"
```

Questions:

- What's the difference between "power redundancy" and "workload redundancy"?
- A single rack failing hits the BMS as `DEGRADED` even though all upstream
  power is `2N_HEALTHY`. Why is that the correct classification?
- In a real DC, what would cause one rack to fail while the rest of the hall
  is fine? (Hint: rack-level cooling, rack-level fire, rack-level breaker,
  rack-internal fault, mistaken human action.)

### Scenario 5 - Dual-Side Failure

Objective: show final failure when both sides lose all power sources.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:FAULT RPM:0"
```

Expected:

- [ ] Both sides lose normal and emergency source power.
- [ ] Both UPS nodes eventually exhaust battery and report `FAULT`.
- [ ] All 8 server racks (`server_rack_a{1..4}` + `_b{1..4}`) reach `FAULT`.
- [ ] `bms/status` shows `mode:FAULT`, `power_state:DOWN`,
      `side_a_health:DOWN`, `side_b_health:DOWN`, `rack_a_state:FAULT`,
      `rack_b_state:FAULT`, `active_alarms:>=4`.
- [ ] Grafana clearly shows simultaneous Side A and Side B failure.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- Which single points of failure still exist in this tabletop model?
- What alerts require immediate escalation?
- How would a real data center prioritize recovery?

### Scenario 6 - LV Switchgear Open Or Trip

Objective: teach breaker isolation, protective trips, and fault-boundary
troubleshooting. `lv_switchgear_a` sits on the 480 V LV bus directly upstream
of the ATS primary input, so opening it cuts the utility path into the ATS.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "OPEN STATUS:OPEN"
```

Optional protective trip:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "STATUS:TRIPPED"
```

Expected:

- [ ] Equipment upstream of switchgear remains normal.
- [ ] Utility path downstream of switchgear loses input.
- [ ] ATS may transfer to generator if the generator path is healthy.
- [ ] Side B remains normal.
- [ ] Trainee can identify the fault boundary.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
```

Questions:

- Why is a breaker opened intentionally during maintenance?
- How is an operator-open breaker different from a protective trip?
- What must be checked before re-closing?

### Scenario 7 - Transformer Overtemperature Or Fault

Objective: connect load, heat, transformer health, and downstream service.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:120 TEMP:185 STATUS:FAULT"
```

Expected:

- [ ] Transformer reports fault and high temperature.
- [ ] Utility path to ATS is lost or derated according to the model.
- [ ] Generator may pick up the side if utility loss is interpreted at the ATS
      and generator is available.
- [ ] Side B remains normal.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:45 TEMP:108 STATUS:NORMAL"
```

Questions:

- What conditions overheat a transformer?
- Why does load percentage matter, not just voltage?
- What downstream symptoms point back to this transformer?

### Scenario 8 - UPS Battery Runtime Drain

Objective: show that UPS is bridge power, not long-term backup.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
```

Optional display acceleration:

```bash
for n in 80 60 40 20 5; do
  mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "BATT:$n STATUS:ON_BATTERY"
  sleep 10
done
```

Expected:

- [ ] `ups_a` shows `ON_BATTERY` while input is unavailable.
- [ ] Battery state is visible on OLED and telemetry.
- [ ] Rack stays online if Side B remains healthy.
- [ ] If both sides are unavailable, rack eventually faults.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
```

Questions:

- What problem does the UPS solve during generator startup?
- What happens if generator startup exceeds UPS runtime?
- Why do technicians monitor battery health during normal operation?

### Scenario 9 - MV Switchgear Trip (Side Disconnect)

Objective: show the cut-off behavior of the MV-bus breaker — the fastest way to
drop an entire side from the 34.5 kV bus without touching the utility or
generator. `mv_switchgear_a` sits downstream of `hv_mv_transformer_a` and
upstream of `mv_lv_transformer_a`, so tripping it isolates everything below the
MV bus while the utility and HV/MV transformer stay energised.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "STATUS:TRIPPED"
```

Expected:

- [ ] `mv_switchgear_a` reports `TRIPPED` (sticky — survives re-energisation).
- [ ] `utility_a` and `hv_mv_transformer_a` stay normal — they're upstream of
      the tripped breaker.
- [ ] Cascade propagates down the side: `mv_lv_transformer_a`, `lv_switchgear_a`
      lose voltage; `ats_a` falls back to `generator_a` (which spins up).
- [ ] Once the generator is `RUNNING`, side-A racks recover via the ATS
      secondary path. If the generator is forced FAULT first, the cascade
      degenerates to Scenario 4.
- [ ] `bms/status` shows `power_state:A_ONLY` initially (utility path lost),
      then `2N_HEALTHY` once the generator picks up.
- [ ] Trainee distinguishes "utility lost" (utility OUTAGE) from "we opened the
      MV bus" (MV switchgear TRIPPED) — both look similar downstream but the
      diagnosis and recovery differ.

Recovery (explicitly reclose the breaker):

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
```

Questions:

- Why does `TRIPPED` survive re-energisation in the broker model? (Hint:
  real breakers require an explicit reset; sticky-fault semantics mirror that.)
- How does an MV-bus trip differ from an LV switchgear trip in terms of scope?
- What protection coordinates between the utility and the MV switchgear?

### Scenario 10 - Cooling Loss And Hot-Aisle Climb

Objective: show that power availability is not enough if cooling is lost.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:0 STATUS:FAULT"
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_b/control" -m "FANS_RUNNING:0 STATUS:FAULT"
```

Expected:

- [ ] Cooling OLEDs show failed fans.
- [ ] `winter-river/facility/status` changes mode.
- [ ] `q_cfm` drops.
- [ ] `hot_aisle_f` rises or becomes unavailable when airflow is zero.
- [ ] Server rack inlet or hot aisle temperature rises if thermal modeling is
      enabled.
- [ ] Grafana shows thermal risk even if power is still present.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:55 STATUS:NORMAL SPEED:60 TEMP:65"
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_b/control" -m "FANS_RUNNING:55 STATUS:NORMAL SPEED:60 TEMP:65"
```

Questions:

- Why can a rack be in danger while power is normal?
- What are hot aisle and cold aisle symptoms?
- Why do cooling alarms have short response windows?

### Scenario 11 - Partial Fan Degradation

Objective: show a non-binary cooling failure and introduce operating margin.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:50 STATUS:NORMAL"
sleep 30
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:45"
sleep 30
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:40"
```

Expected:

- [ ] `fans_running_a` decreases in telemetry.
- [ ] Airflow metrics decrease.
- [ ] Hot aisle temperature changes gradually.
- [ ] Cooling may become `DEGRADED` before total failure.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:55 STATUS:NORMAL SPEED:60 TEMP:65"
```

Questions:

- Why monitor individual fan failures?
- What does cooling margin mean?
- How is partial degradation different from total loss?

### Scenario 12 - Transformer Load Imbalance

Objective: teach load distribution between sides and the warning vs fault
distinction at the MV/LV transformer.

Trigger (push side A toward overload while side B stays normal):

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:88 TEMP:165 STATUS:WARNING"
```

Expected:

- [ ] `mv_lv_transformer_a` reports `WARNING` with elevated load and temp.
- [ ] All 4 side-A racks remain `NORMAL` — `WARNING` doesn't trip the chain.
- [ ] Push further to drive a real fault:
      `mosquitto_pub -t "winter-river/mv_lv_transformer_a/control" -m "STATUS:FAULT"`
      and observe the side-A cascade (matches Scenario 4 expectations).
- [ ] Trainee can explain how transformer load is measured and what triggers
      a real trip versus a warning.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "LOAD:45 TEMP:108 STATUS:NORMAL"
```

Questions:

- What is the difference between transformer warning and fault?
- Why monitor load percentage and temperature separately?
- In block-redundant 2N, why doesn't side-A's transformer load affect side-B?

### Scenario 13 - Cooling-Only Fault (Power-Independent Alarm)

Objective: show that not every fault is a power problem. Killing fans on a
single side degrades cooling without affecting any rack's power feed —
trainees learn to read `cooling_state` separately from `power_state` in BMS.

Trigger (drop side-A fans to half capacity):

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:25 STATUS:DEGRADED"
```

Expected:

- [ ] `cooling_a` reports `fans_running:25` (down from 55).
- [ ] `facility/status` recomputes — `fan_count` becomes 80 (25 + 55),
      `q_cfm` drops, `hot_aisle_f` climbs.
- [ ] `bms/status` shows `mode:DEGRADED`, `power_state:2N_HEALTHY`
      (unchanged — this is a cooling alarm, not a power alarm),
      `cooling_state:DEGRADED`.
- [ ] All 8 racks remain `NORMAL` (power is fine; they may report higher
      `inlet_f` via thermal broadcast).
- [ ] Trainee can articulate that thermal alarms have different urgency
      curves than power alarms — heat takes minutes to become critical
      whereas power loss is immediate.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:55 STATUS:NORMAL"
```

Questions:

- Why does the BMS treat cooling state separately from power state?
- What information belongs in a shift handoff when cooling is degraded but
  IT is unaffected?
- Why are cooling response windows shorter than power response windows in
  high-density halls?

### Scenario 14 - Broker Loss, Management Plane Dark

Objective: show what happens when MQTT fails but local device firmware still
exists.

Trigger:

```bash
sudo systemctl stop mosquitto
```

Expected:

- [ ] Nodes log MQTT reconnect attempts.
- [ ] OLEDs continue showing last-known local state.
- [ ] Grafana panels go stale.
- [ ] Telegraf logs disconnect.
- [ ] LWT messages do not fire while the broker itself is down because no broker
      is available to publish them.

Recovery:

```bash
sudo systemctl start mosquitto
```

- [ ] Nodes reconnect within about 30 seconds.
- [ ] Retained status messages repopulate.
- [ ] Grafana resumes updating.

Questions:

- What is the difference between control plane and physical power plane?
- What can operators still learn from local OLEDs?
- Why should firmware not depend entirely on the network?

### Scenario 15 - Hotspot Or WiFi Loss

Objective: test autonomous node recovery after the AP disappears.

Trigger:

```bash
sudo ./scripts/setup_hotspot.sh stop
```

Expected:

- [ ] Nodes drop WiFi.
- [ ] Nodes retry and restart according to firmware timeout behavior.
- [ ] MQTT telemetry stops while the hotspot is down.

Recovery:

```bash
sudo ./scripts/setup_hotspot.sh start
```

- [ ] Nodes rejoin without reflashing.
- [ ] MQTT telemetry resumes.
- [ ] Grafana recovers after fresh data arrives.

Questions:

- Why must the AP use 2.4 GHz?
- What should a technician check first when all nodes disappear at once?
- How is WiFi loss different from power-chain failure?

### Scenario 16 - BMS Aggregator Sanity

Objective: confirm the broker-published `winter-river/bms/status` aggregate
correctly reflects induced upstream state, and that trainees can read it as
a single rollup rather than chasing 26 per-node OLEDs.

Trigger (induce one degraded condition that affects multiple fields):

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:0 STATUS:ON_BATTERY"
```

Expected on `winter-river/bms/status`:

- [ ] `mode` rises to `DEGRADED` or `ALARM` (depending on cascade depth).
- [ ] `power_state` reflects per-side UPS health (`A_ONLY` if `ups_a` is
      fully out, or `2N_HEALTHY` while it's still on battery).
- [ ] `side_a_health` reports `DEGRADED` once an upstream non-normal state
      appears, `DOWN` when the chain fully drops.
- [ ] `rack_a_state` rolls up worst-of across the 4 a-racks
      (`NORMAL → DEGRADED` while UPS is on battery → `FAULT` if it drains).
- [ ] `active_alarms` count matches the number of non-normal nodes (excluding
      generators in STANDBY).
- [ ] The bms ESP32 board (if flashed) renders the same state on its OLED.
- [ ] Operator can use BMS alone to triage without reading individual nodes.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
```

Note: `bms` is broker-driven. Manual publishes to `winter-river/bms/control`
are no-ops — the broker recomputes `winter-river/bms/status` every tick from
live node state and ignores incoming control commands on that topic.

Questions:

- What does the BMS aggregator tell you that the per-node OLEDs don't?
- Why is `power_state` derived from per-side UPS health rather than from the
  full upstream chain?
- When the BMS shows `mode:ALARM` but `power_state:2N_HEALTHY`, what is the
  fault category (hint: cooling / rack-level / facility, not power)?

### Scenario 17 - Cascading Multi-Fault Incident

Objective: show that real incidents often involve stacked faults.

Trigger, with about 10 seconds between each command:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "FUEL:0 STATUS:FAULT"
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_b/control" -m "FANS_RUNNING:20 STATUS:DEGRADED"
```

Expected:

- [ ] Side A loses source and standby backup.
- [ ] Side B remains powered.
- [ ] Cooling is degraded.
- [ ] Server rack is `DEGRADED`.
- [ ] Thermal risk grows while power redundancy is already reduced.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "FUEL:85 STATUS:STANDBY RPM:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_b/control" -m "FANS_RUNNING:55 STATUS:NORMAL SPEED:60 TEMP:65"
```

Questions:

- What should be stabilized first?
- What information belongs in an incident timeline?
- Why can the first visible alarm be different from the root cause?

### Scenario 18 - Full Recovery And Return To Service

Objective: confirm clean recovery and remove stale state.

Steps:

- [ ] Restore utilities to `GRID_OK`.
- [ ] Reclose both MV switchgear nodes (`CLOSE STATUS:CLOSED`).
- [ ] Restore generators to `STANDBY`.
- [ ] Reclose LV switchgear.
- [ ] Restore transformer status and temperature.
- [ ] Restore ATS source to `UTILITY`.
- [ ] Restore UPS input and battery level.
- [ ] Restore cooling to nominal (`FANS_RUNNING:55 STATUS:NORMAL`) on both sides.
- [ ] Restore all 8 server racks to `INPUT:480 STATUS:NORMAL`.
- [ ] Confirm `bms/status` returns to `mode:NORMAL`,
      `power_state:2N_HEALTHY`, `redundancy_lost:0`, `active_alarms:0`,
      `rack_a_state:NORMAL`, `rack_b_state:NORMAL`.
      The BMS is your single sign-off check — if it's NORMAL, the whole rig
      is consistent.
- [ ] Run `./scripts/status.sh`.
- [ ] Check Grafana for stale alarms.

Expected:

- [ ] All required nodes return to normal.
- [ ] No retained `OFFLINE`, `FAULT`, or `DEGRADED` state remains for active
      nodes.
- [ ] Dashboard and OLED state agree.
- [ ] Recovery order can be explained upstream to downstream.

Questions:

- Why is clearing an alarm not the same as fixing the cause?
- Why should recovery be verified upstream to downstream?
- What should be recorded after a failure test?

## Part 4 - Instructor Question Bank

Use these for oral checks, worksheet prompts, and demo discussion.

### 4.1 Topology And Identification

1. Trace power from `utility_a` down through `hv_mv_transformer_a` →
   `mv_switchgear_a` → `mv_lv_transformer_a` → `lv_switchgear_a` →
   `ats_a` → `ups_a` → `server_rack_a{1..4}`. Where does cooling branch off?
2. What is the output voltage at each stage?
3. Which nodes are duplicated on Side A and Side B (there should be 13 each)?
4. Why are there **no** shared nodes in this topology? What changed between
   the old shared-rectifier design and the current block-redundant 2N?
5. What is the rack input voltage and current type? Where does AC→DC
   conversion happen now that the rectifier was removed?
6. Which nodes are source equipment, distribution equipment, IT loads, and
   facility-aggregation (BMS)?
7. What is the difference between `winter-river/facility/status` and
   `winter-river/bms/status`? Which one would an electrical engineer read
   first? Which would an operations manager read first?

### 4.2 Redundancy Concepts

1. What does **block-redundant 2N** mean in this system? How does it differ
   from rack-level 2N (where each rack has dual AC feeds)?
2. How does 2N differ from N+1?
3. What does `DEGRADED` mean for a rack? When does the broker promote a rack
   from `NORMAL` to `DEGRADED`? (Hint: look at the parent UPS state.)
4. Which failure removes redundancy but doesn't drop any rack? (Hint: think
   about utility loss with generator still healthy.)
5. Which combination of failures drops half the racks but not the other half?
6. Which combination drops all 8 racks at once? (Hint: with no shared node,
   this requires faults on both sides.)
7. What is the difference between **power redundancy** (2N power feeds) and
   **workload redundancy** (multiple racks running mirrored workloads)? Which
   does Winter River simulate, and how do they layer?
8. Which single points of failure still exist in this tabletop model?
   (Hint: the broker itself, MQTT, the Pi, the WiFi AP — but no longer any
   power-domain SPOF.)
9. Why should risky maintenance stop when only one side is healthy, even
   though half the racks are still powered?

### 4.3 Failure Modes

1. What does the ATS do when utility power is lost?
2. Why does generator power not appear instantly?
3. What bridges the generator startup gap?
4. How is a breaker trip different from a utility outage?
5. What happens when a transformer overheats?
6. What happens when UPS runtime expires?
7. Can healthy AC equipment save the rack if the DC bus is unavailable?

### 4.4 Cooling And Thermal

1. Why is cooling failure urgent even when power is available?
2. What are hot aisle and cold aisle symptoms?
3. What is PUE?
4. Why is PUE not 1.0?
5. What happens when fan count drops gradually?
6. Why monitor cooling margin instead of waiting for total failure?

### 4.5 Protocol And Software

1. What is MQTT publish/subscribe?
2. What is MQTT Last Will and Testament?
3. Why are status messages retained?
4. Why are control commands not retained?
5. What happens if Mosquitto stops?
6. What happens if the WiFi hotspot stops?
7. What data moves through Telegraf?
8. What data is stored in InfluxDB?
9. What can Grafana show that a single OLED cannot?
10. What can an OLED show when Grafana is unavailable?
11. The `bms` node's firmware just renders state — all aggregation logic
    runs in the broker. Why is that the correct split? What would go wrong
    if the BMS firmware tried to compute its own rollup from MQTT?
12. If `bms` says `mode:NORMAL` but a single node's OLED shows `FAULT`,
    which one is wrong, and how do you find out?

### 4.6 Technician Troubleshooting

1. A node OLED is blank. What do you check?
2. A node has WiFi but no MQTT telemetry. What do you check?
3. MQTT has telemetry but Grafana is stale. What do you check?
4. A retained `OFFLINE` message will not clear. What do you check?
5. A control command does nothing. What topic and payload do you verify?
6. A generator never starts. Which upstream states and timers do you inspect?
7. Only `server_rack_a1` is `FAULT`; `_a2`, `_a3`, `_a4` and all four
   b-racks are `NORMAL`. What's the inspection order? (Hint: the side's
   UPS and chain are clearly fine — look at the rack-local CPU, inlet
   temp, or status_msg.)
8. All 4 `server_rack_a*` are `FAULT` but all 4 `server_rack_b*` are
   `NORMAL`. What's the inspection order? (Hint: side-A's `ups_a` /
   `ats_a` / generator / utility / MV switchgear are the failure domain.)
9. All 8 racks are `FAULT`. What's the inspection order? (Hint: this
   requires faults on both sides — start with the broker and MQTT first
   to rule out a management-plane mirage, then sweep both sides upstream.)
9. The `bms` shows `mode:ALARM` and `active_alarms:3` but every individual
   node OLED shows `NORMAL`. What's happening, and what do you check first?
10. What information belongs in an incident handoff?

### 4.7 Design Stretch

1. Why does the simulation engine run on the Pi instead of every ESP32?
2. How would the model change for 2N+1?
3. What is missing compared with a real data center?
4. How would you model rack-level hot spots?
5. How would you add generator paralleling?
6. How would you make MQTT authentication production-ready?

## Part 5 - Pass / Fail Criteria

A full system run passes when all of these are true:

- [ ] All required physical nodes boot and publish telemetry.
- [ ] Pi hotspot remains on 2.4 GHz and keeps ESP32 clients connected.
- [ ] MQTT accepts status and control traffic.
- [ ] LWT offline detection works for at least one unplug test.
- [ ] Simulation engine propagates a Side A failure without affecting Side B.
- [ ] Generator startup delay and ATS transfer are visible.
- [ ] UPS ride-through or battery state is visible.
- [ ] All 4 `server_rack_b*` stay `NORMAL` during a Side A power failure;
      all 4 `server_rack_a*` cascade to `FAULT` (block-redundant 2N
      semantics — no rack-level dual feed).
- [ ] A single-rack fault (`server_rack_a1` only) drops *that* rack to
      `FAULT` while the other 7 stay `NORMAL` — proves workload-level
      redundancy is independent of power-level redundancy.
- [ ] All 8 racks fail or report no power during dual-side loss.
- [ ] MV switchgear `TRIPPED` on one side cascades the same way as utility
      OUTAGE on that side (after generator picks up, side recovers).
- [ ] `bms/status` reflects every scenario within ~2 ticks: `mode`,
      `power_state`, `redundancy_lost`, `rack_a_state`, `rack_b_state`,
      `active_alarms` all match what individual node telemetry shows.
- [ ] Cooling failure is visible as a separate operational risk.
- [ ] `winter-river/facility/status` mode transitions are observed at least
      once (e.g. `NORMAL` -> `OVERHEATING` or `UNDERPRESSURED` -> `FAULT`),
      and `pue` stays in a plausible band (roughly 1.05-1.30) during normal
      baseline operation.
- [ ] Broker-loss and WiFi-loss behavior are understood.
- [ ] Grafana reflects at least one normal state and one fault state.
- [ ] Recovery returns all required nodes to normal.
- [ ] Trainee can answer the core 2N, ATS, UPS, cooling, and MQTT questions.

## Part 6 - Issue Log Template

```text
Issue ID:
Scenario:
Observed time:
Expected behavior:
Actual behavior:
Affected node(s):
OLED state:
MQTT topic and payload:
Grafana panel:
Broker log line:
Likely layer:
  [ ] Physical wiring / power
  [ ] ESP32 firmware
  [ ] WiFi / hotspot
  [ ] MQTT broker
  [ ] Simulation engine
  [ ] Database
  [ ] Telegraf / InfluxDB
  [ ] Grafana
Fix attempted:
Retest result:
Owner:
```

## Part 7 - Demo Sign-Off

Before handing the rig to a non-developer:

- [ ] Part 1 pre-flight completed.
- [ ] Part 2 smoke tests completed (including 2.9 BMS aggregator test).
- [ ] At least Scenarios 0, 1, 4 (full side failure), 4b (single-rack
      failure), 9 (MV switchgear trip), 10 (cooling loss), 14 (broker loss),
      and 18 (recovery) rehearsed today.
- [ ] Recovery commands for rehearsed scenarios are queued or printed.
- [ ] Grafana dashboard is open and showing live data.
- [ ] Printed copy of this checklist is available.
- [ ] Known missing modules are written in the run record.

```text
Demo lead:
Date:
Pass / needs work:
Notes:
```

## Part 8 - Cleanup After Testing

- [ ] Restore all simulated faults to normal.
- [ ] Confirm no retained `OFFLINE`, `FAULT`, or `DEGRADED` messages remain for
      active nodes.
- [ ] Stop live MQTT tail terminals.
- [ ] Save scenario notes and issue logs.
- [ ] Export or screenshot Grafana evidence if needed.
- [ ] Power down ESP32 nodes if the setup will be moved.
- [ ] Leave the Pi running only if needed for dashboard access or debugging.
- [ ] Document modules that failed, overheated, or required reflashing.
