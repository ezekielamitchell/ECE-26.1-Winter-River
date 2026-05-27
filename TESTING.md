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
Spare / empty slot IDs:
Legacy modules in spare slots (pdu_*, rectifier_a/b, monitoring_*, etc.):
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

The current broker/database simulation topology uses a shared 2N rectifier
feeding two independent server racks, plus a shared BMS aggregator:

```text
Side A:
utility_a -> hv_mv_transformer_a -> mv_switchgear_a -> mv_lv_transformer_a
generator_a ------------------------------------------^
ats_a -> lv_dist_a -> ups_a -----------.
lv_dist_a -> cooling_a                 |
lv_dist_a -> lighting_a                |
                                       v
                                rectifier (PATH_A + PATH_B)
                                  |             |
                                  v             v
                            server_rack_a   server_rack_b
                                  ^             ^
Side B:                           |             |
utility_b -> hv_mv_transformer_b -> mv_switchgear_b -> mv_lv_transformer_b
generator_b ------------------------------------------^
ats_b -> lv_dist_b -> ups_b -----------'
lv_dist_b -> cooling_b
lv_dist_b -> lighting_b

bms (shared) <-- subscribes to every winter-river/+/status topic
            --> publishes rolled-up winter-river/bms/status
```

Each server rack reads BOTH rectifier paths independently. A single rack
reports `DEGRADED` when one of its two paths is down; both racks fail only
if the shared rectifier loses both paths. This mirrors a real data hall with
multiple dual-fed racks.

Active node IDs (24 physical boards = 23 seeded in the DB + 1 broker-synthesized):

| Group | Node IDs |
|---|---|
| Side A (11) | `utility_a`, `hv_mv_transformer_a`, `mv_switchgear_a`, `mv_lv_transformer_a`, `generator_a`, `ats_a`, `lv_dist_a`, `ups_a`, `cooling_a`, `lighting_a`, `server_rack_a` |
| Side B (11) | `utility_b`, `hv_mv_transformer_b`, `mv_switchgear_b`, `mv_lv_transformer_b`, `generator_b`, `ats_b`, `lv_dist_b`, `ups_b`, `cooling_b`, `lighting_b`, `server_rack_b` |
| Shared, DB-seeded (1) | `rectifier` |
| Shared, broker-synthesized (1) | `bms` (no row in `nodes` — broker publishes `winter-river/bms/status` from live state every tick) |

Slot accounting: **24 physical slots populated / 24 baseplate slots / 0 spare.**
Both sides perfectly mirrored at 11 each. No spare expansion slots — any new
node type requires retiring an existing one or expanding the baseplate.

DB-row accounting: `nodes` table holds **23 rows** (10 Side A + 10 Side B +
3 shared: `rectifier`, `server_rack_a`, `server_rack_b`). `bms` is intentionally
absent from `nodes` — it is a broker-published synthetic topic like
`facility/status`. So the broker's `online_nodes` ceiling is **23**, not 24.

Compatibility note:

- Some firmware folders and older docs still mention `pdu_a`, `pdu_b`,
  `rectifier_a`, `rectifier_b`, `monitoring_a`, `monitoring_b`, and the
  legacy single shared `server_rack`.
- If those modules are physically installed for a demo, document whether they
  are part of the active broker/database topology or only display-only modules.
- If a node publishes telemetry but is not present in the `nodes` table, the
  broker should ignore it instead of failing.

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
- [ ] `server_rack_a` and `server_rack_b` are clearly marked as the IT loads,
      one per side. The shared `rectifier` between them is clearly labeled.
- [ ] Workspace ambient temperature is logged: ______ deg F.
- [ ] Spare USB cable, laptop, and serial monitor are available.
- [ ] Baseplate slot accounting: 24 total slots / 24 expected populated /
      0 spare under the current topology. Any empty slot signals a missing
      node — record which one(s) in the Run Record before continuing.

### 1.2 Node Inventory

The baseplate has **24 physical slots** and the active rig fills **all 24** of
them — 11 Side A + 11 Side B + 2 shared (`rectifier`, `bms`). No spare slots.
Any empty slot means a missing board; record it in the Run Record.

Of those 24 boards, 23 are seeded in `nodes` (broker validates telemetry
against this table); `bms` is broker-synthesized and has no DB row.

Physical board inventory (24 boards):

| # | node_id | Powered | OLED | WiFi | MQTT | Notes |
|---|---|---|---|---|---|---|
| 1 | `utility_a` | [ ] | [ ] | [ ] | [ ] | |
| 2 | `hv_mv_transformer_a` | [ ] | [ ] | [ ] | [ ] | |
| 3 | `mv_switchgear_a` | [ ] | [ ] | [ ] | [ ] | |
| 4 | `mv_lv_transformer_a` | [ ] | [ ] | [ ] | [ ] | |
| 5 | `generator_a` | [ ] | [ ] | [ ] | [ ] | |
| 6 | `ats_a` | [ ] | [ ] | [ ] | [ ] | |
| 7 | `lv_dist_a` | [ ] | [ ] | [ ] | [ ] | |
| 8 | `ups_a` | [ ] | [ ] | [ ] | [ ] | |
| 9 | `cooling_a` | [ ] | [ ] | [ ] | [ ] | |
|10 | `lighting_a` | [ ] | [ ] | [ ] | [ ] | |
|11 | `server_rack_a` | [ ] | [ ] | [ ] | [ ] | dual-fed from shared rectifier |
|12 | `utility_b` | [ ] | [ ] | [ ] | [ ] | |
|13 | `hv_mv_transformer_b` | [ ] | [ ] | [ ] | [ ] | |
|14 | `mv_switchgear_b` | [ ] | [ ] | [ ] | [ ] | |
|15 | `mv_lv_transformer_b` | [ ] | [ ] | [ ] | [ ] | |
|16 | `generator_b` | [ ] | [ ] | [ ] | [ ] | |
|17 | `ats_b` | [ ] | [ ] | [ ] | [ ] | |
|18 | `lv_dist_b` | [ ] | [ ] | [ ] | [ ] | |
|19 | `ups_b` | [ ] | [ ] | [ ] | [ ] | |
|20 | `cooling_b` | [ ] | [ ] | [ ] | [ ] | |
|21 | `lighting_b` | [ ] | [ ] | [ ] | [ ] | |
|22 | `server_rack_b` | [ ] | [ ] | [ ] | [ ] | dual-fed from shared rectifier |
|23 | `rectifier` | [ ] | [ ] | [ ] | [ ] | shared 2N, feeds both racks |
|24 | `bms` | [ ] | [ ] | [ ] | [ ] | aggregator; broker-synthesized (no DB row); OLED mirrors broker state |

Optional or legacy physical modules:

| node_id | Installed | In database | MQTT accepted | Notes |
|---|---|---|---|---|
| `pdu_a` | [ ] | [ ] | [ ] | legacy per-side PDU |
| `pdu_b` | [ ] | [ ] | [ ] | legacy per-side PDU |
| `rectifier_a` | [ ] | [ ] | [ ] | superseded by shared `rectifier` |
| `rectifier_b` | [ ] | [ ] | [ ] | superseded by shared `rectifier` |
| `monitoring_a` | [ ] | [ ] | [ ] | superseded by shared `bms` |
| `monitoring_b` | [ ] | [ ] | [ ] | superseded by shared `bms` |
| `server_rack` (single) | [ ] | [ ] | [ ] | superseded by `server_rack_a` + `server_rack_b` |

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
pio run -e generator_a
pio run -e ats_a
pio run -e ups_a
pio run -e server_rack_a
pio run -e server_rack_b
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
- [ ] Both HV/MV transformers show normal pass-through.
- [ ] Both MV switchgear nodes show closed/normal.
- [ ] Both MV/LV transformers show normal temperature and 480 V output.
- [ ] Both generators show `STANDBY`, not `RUNNING`.
- [ ] Both ATS nodes use `UTILITY`.
- [ ] Both LV distribution nodes show normal input and load.
- [ ] Both UPS nodes show `NORMAL`, 480 V input, 480 V output, and healthy
      battery.
- [ ] Shared rectifier shows `NORMAL`, `PATH_A:1`, and `PATH_B:1`.
- [ ] `server_rack_a` shows `NORMAL` with `PATH_A:1 PATH_B:1`.
- [ ] `server_rack_b` shows `NORMAL` with `PATH_A:1 PATH_B:1`.
- [ ] Cooling is normal on both sides.
- [ ] Lighting is normal on both sides.
- [ ] `bms` shows `mode:NORMAL`, `power_state:2N_HEALTHY`, `redundancy_lost:0`,
      `active_alarms:0`, `online_nodes:23` (`bms` itself is not in `nodes`, so
      the ceiling is 23 even when the rig is fully populated).
- [ ] Grafana shows no stale fault state from a previous run.

Suggested baseline commands:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_b/control" -m "CLOSE STATUS:CLOSED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_a/control" -m "STATUS:NORMAL TEMP:108 LOAD:45"
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_lv_transformer_b/control" -m "STATUS:NORMAL TEMP:108 LOAD:45"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 LOAD:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:STANDBY RPM:0 LOAD:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_a/control" -m "SOURCE:UTILITY STATUS:UTILITY"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ats_b/control" -m "SOURCE:UTILITY STATUS:UTILITY"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_a/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/ups_b/control" -m "INPUT:480 BATT:100 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/rectifier/control" -m "INPUT_AC:480 PATH_A:1 PATH_B:1 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_a/control" -m "PATH_A:1 PATH_B:1 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_b/control" -m "PATH_A:1 PATH_B:1 STATUS:NORMAL"
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

- [ ] Subscribe to `winter-river/lighting_a/status`.
- [ ] Unplug `lighting_a`.
- [ ] Broker publishes retained `{"node":"lighting_a","status":"OFFLINE"}`.
- [ ] Reconnect `lighting_a`.
- [ ] Node publishes `ONLINE` or normal telemetry.
- [ ] Repeat with a critical node such as `utility_a` only if cascade testing is
      approved.

Command:

```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/lighting_a/status" -v
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
- [ ] `online_nodes` equals the count of DB-seeded active nodes — **23** under
      the current topology (`bms` is broker-synthesized and not in `nodes`,
      so it does not count toward `online_nodes`); less if any node is missing.
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

- [ ] Trainee traces Side A from `utility_a` down to the shared `rectifier`,
      and notes that the rectifier feeds BOTH `server_rack_a` and
      `server_rack_b` (each rack reads both rectifier paths).
- [ ] Trainee traces Side B from `utility_b` down to the shared `rectifier`
      and confirms the same dual-rack feed.
- [ ] Trainee identifies generators, ATS units, UPS units, shared rectifier,
      cooling, lighting, both server racks, and the `bms` aggregator.
- [ ] Trainee reads `winter-river/bms/status` and confirms the rolled-up
      `power_state:2N_HEALTHY`, `rack_a_state:NORMAL`, `rack_b_state:NORMAL`
      matches what the individual node OLEDs show.
- [ ] Operator compares OLEDs, MQTT, and Grafana.

Expected:

- [ ] Both utility paths are available.
- [ ] Generators are in `STANDBY`.
- [ ] Shared rectifier shows both paths live.
- [ ] Server rack is `NORMAL`.
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

### Scenario 4 - Full Side Failure, 2N Degraded Operation

Objective: prove that one full side can fail without dropping the IT load.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
```

Expected:

- [ ] Side A cannot feed the shared rectifier.
- [ ] Side B stays normal and continues feeding the rectifier.
- [ ] Rectifier shows `PATH_A:0`, `PATH_B:1`.
- [ ] **Both** `server_rack_a` and `server_rack_b` report `DEGRADED` (each
      rack lost one of its two paths but kept the other). Neither rack
      reports `FAULT`.
- [ ] `bms/status` shows `mode:ALARM`, `power_state:B_ONLY`,
      `redundancy_lost:1`, `side_a_health:DOWN`, `side_b_health:OK`.
      (Mode is `ALARM` rather than `DEGRADED` because the Side A cascade puts
      multiple nodes — utility_a, generator_a, and the downstream chain — into
      fault states, pushing `active_alarms` above 1. The broker's
      `_compute_bms` promotes `DEGRADED` → `ALARM` once `active_alarms > 1`.)
- [ ] Operators can explain that **both racks** are still powered but
      redundancy is gone — a second fault on Side B now drops both racks,
      not just one. This is why "degraded" requires immediate attention even
      though nothing is offline.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- What does 2N mean in practical terms?
- How is 2N different from N+1?
- Why is degraded operation still urgent?

### Scenario 4b - Single Rack Failure (Workload-Level Redundancy)

Objective: show that losing one rack is not the same as losing compute —
the *other* rack picks up workload. This is the teaching unlocked by the
split-rack topology.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_a/control" -m "STATUS:FAULT PATH_A:0 PATH_B:0"
```

Expected:

- [ ] `server_rack_a` reports `FAULT` with both paths down.
- [ ] `server_rack_b` stays `NORMAL` — power chain on both sides is healthy,
      shared rectifier is fine; only this one rack went down.
- [ ] `bms/status` shows `mode:DEGRADED`, `power_state:2N_HEALTHY` (power is
      still fine — this isn't a power problem), `rack_a_state:FAULT`,
      `rack_b_state:NORMAL`, `active_alarms:>=1`.
- [ ] If you're running real workloads on top, the trainee should
      articulate that VMs/containers should have already failed over to
      `server_rack_b` — this is workload-level redundancy, distinct from
      power-level redundancy.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/server_rack_a/control" -m "STATUS:NORMAL PATH_A:1 PATH_B:1"
```

Questions:

- What's the difference between "power redundancy" and "workload redundancy"?
- A single rack failing hits the BMS as `DEGRADED` even though all upstream
  power is `2N_HEALTHY`. Why is that the correct classification?
- In a real DC, what would cause one rack to fail while the rest of the hall
  is fine? (Hint: rack-level cooling, rack-level fire, rack-level breaker,
  rack-internal fault, mistaken human action.)

### Scenario 5 - Dual-Side Failure

Objective: show final failure when both redundant paths are lost.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:FAULT RPM:0"
```

Expected:

- [ ] Both sides lose normal and emergency source power.
- [ ] UPS nodes eventually exhaust battery or report fault according to model.
- [ ] Rectifier reports no usable path (`PATH_A:0 PATH_B:0`).
- [ ] **Both** `server_rack_a` and `server_rack_b` reach `FAULT` — there is
      no surviving rack because the shared rectifier has no input.
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

### Scenario 6 - MV Switchgear Open Or Trip

Objective: teach breaker isolation, protective trips, and fault-boundary
troubleshooting.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "OPEN STATUS:OPEN"
```

Optional protective trip:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "STATUS:TRIPPED"
```

Expected:

- [ ] Equipment upstream of switchgear remains normal.
- [ ] Utility path downstream of switchgear loses input.
- [ ] ATS may transfer to generator if the generator path is healthy.
- [ ] Side B remains normal.
- [ ] Trainee can identify the fault boundary.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/mv_switchgear_a/control" -m "CLOSE STATUS:CLOSED"
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

### Scenario 9 - Shared Rectifier Or DC Bus Loss

Objective: remind trainees that the rack ultimately needs 48 V DC, not only
healthy upstream AC.

Preferred engine-driven trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:FAULT RPM:0"
```

Manual override of the rectifier ESP32 directly, useful when the broker is
stopped (with the broker running, this gets overwritten on the next
simulation tick when power propagation recomputes rectifier state):

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/rectifier/control" -m "INPUT_AC:0 PATH_A:0 PATH_B:0 STATUS:OFF"
```

Expected:

- [ ] Shared rectifier reports no usable AC input and 0 V DC output.
- [ ] **Both** `server_rack_a` and `server_rack_b` report `FAULT` — this is
      the only single-component failure that drops both racks at once, which
      is why the shared rectifier is the system's biggest single point of
      failure even with full 2N upstream.
- [ ] `bms/status` shows `mode:FAULT`, `power_state:DOWN`,
      `rack_a_state:FAULT`, `rack_b_state:FAULT`.
- [ ] Trainee distinguishes AC-side health from DC delivery to the rack.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_b/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_b/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

Questions:

- What does the rectifier convert?
- Why is DC delivery its own reliability concern?
- Can healthy AC equipment save the rack if the DC bus is unavailable?

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

### Scenario 12 - PDU Or Branch Overload

Objective: teach branch-circuit loading and overload protection.

Use this scenario only when `pdu_a` / `pdu_b` are active in the physical or
database topology. If PDUs are not active, use the LV distribution fallback.

PDU trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/pdu_a/control" -m "LOAD:98 STATUS:OVERLOAD"
```

LV distribution fallback:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_dist_a/control" -m "UPS:160 MECH:80 STATUS:FAULT"
```

Expected:

- [ ] Overload state is visible.
- [ ] Operators can identify which branch is affected.
- [ ] Server rack may remain online if redundant power remains available.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/pdu_a/control" -m "LOAD:25 STATUS:NORMAL"
mosquitto_pub -h 192.168.4.1 -t "winter-river/lv_dist_a/control" -m "UPS:95 MECH:42 STATUS:NORMAL SOURCE:UTILITY"
```

Questions:

- Why is load balance important?
- What is the difference between high utilization and overload?
- Why should technicians avoid moving load blindly?

### Scenario 13 - Lighting Or Facility Load Fault

Objective: show that not every facility fault drops IT power, but it can still
affect operations and safety.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "DIM:25 STATUS:DIMMED"
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "INPUT:0 STATUS:OFF"
```

Expected:

- [ ] Lighting fault is visible.
- [ ] Server rack remains powered.
- [ ] Trainee can explain why facility support systems matter.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/lighting_a/control" -m "INPUT:277 DIM:100 STATUS:NORMAL"
```

Questions:

- What work should stop when lighting is unsafe?
- What information belongs in a shift handoff?
- Why is facility state part of operational readiness?

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

### Scenario 16 - Monitoring Or BMS Alert

Objective: show how monitoring changes operator response even when power remains
present.

Use this only when `monitoring_a` / `monitoring_b` are installed and accepted by
the active topology.

Trigger:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/monitoring_a/control" -m "SENSORS:10 STATUS:ALERT"
```

Expected:

- [ ] Monitoring alert is visible.
- [ ] Power path does not change unless the model links the alert to a fault.
- [ ] Trainee understands monitoring as visibility, not primary power equipment.

Recovery:

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/monitoring_a/control" -m "SENSORS:12 STATUS:NORMAL"
```

Questions:

- What is the difference between a sensor alert and equipment failure?
- What extra information is needed before dispatching a technician?
- Why is monitoring critical during normal operation?

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
- [ ] Restore generators to `STANDBY`.
- [ ] Close switchgear.
- [ ] Restore transformer status and temperature.
- [ ] Restore ATS source to `UTILITY`.
- [ ] Restore UPS input and battery level.
- [ ] Restore rectifier path state (`PATH_A:1 PATH_B:1 STATUS:NORMAL`).
- [ ] Restore cooling, lighting, and monitoring if installed.
- [ ] Restore **both** `server_rack_a` and `server_rack_b` to
      `PATH_A:1 PATH_B:1 STATUS:NORMAL`.
- [ ] Confirm `bms/status` returns to `mode:NORMAL`,
      `power_state:2N_HEALTHY`, `redundancy_lost:0`, `active_alarms:0`.
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

1. Trace power from `utility_a` to `server_rack_a` AND `server_rack_b`.
   How does the same Side A chain reach both racks?
2. What is the output voltage at each stage?
3. Which nodes are duplicated on Side A and Side B (there should be 11 each)?
4. Which nodes are shared? Why does `rectifier` stay shared but `server_rack`
   was split into `_a` and `_b`?
5. What is the rack input voltage and current type?
6. Which nodes are source equipment, distribution equipment, IT loads, and
   facility-aggregation (BMS)?
7. What is the difference between `winter-river/facility/status` and
   `winter-river/bms/status`? Which one would an electrical engineer read
   first? Which would an operations manager read first?

### 4.2 Redundancy Concepts

1. What does 2N mean in this system?
2. How does 2N differ from N+1?
3. What does `DEGRADED` mean if a rack is still powered?
4. Which failure removes redundancy but does not drop either rack?
5. Which combination of failures drops one rack but not the other?
6. Which single failure drops both racks at once? (Hint: it's not utility,
   not generator, not UPS — it's shared.)
7. What is the difference between **power redundancy** (2N power feeds) and
   **workload redundancy** (two racks running mirrored workloads)? Which
   does Winter River simulate, and how do they layer?
8. Which single points of failure still exist in this tabletop model?
9. Why should risky maintenance stop when only one path is healthy, even
   though both racks are technically still powered?

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
7. Only `server_rack_a` is `FAULT` and `server_rack_b` is `NORMAL`. What's
   the inspection order? (Hint: the shared rectifier is fine — if it
   weren't, both racks would be down. Look at rack-local issues first.)
8. Both `server_rack_a` AND `server_rack_b` are `FAULT`. What's the
   inspection order now? (Hint: this is rectifier-or-upstream territory.)
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
- [ ] Both `server_rack_a` and `server_rack_b` remain online during a
      single-side power failure (each reports `DEGRADED`, neither `FAULT`).
- [ ] A single-rack fault (`server_rack_a` only) drops *that* rack to
      `FAULT` while the other stays `NORMAL` — proves workload-level
      redundancy is independent of power-level redundancy.
- [ ] Both racks fail or report no power during dual-side loss or shared
      rectifier loss.
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
- [ ] At least Scenarios 0, 1, 4, 4b (single-rack failure), 9 (rectifier
      loss), 10, 14, and 18 rehearsed today.
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
