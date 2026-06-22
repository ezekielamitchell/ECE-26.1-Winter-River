# Winter River System Verification and Scenario Runbook

**Project:** ECE 26.1 Winter River

**Status:** Completed and delivered to AWS, June 2026

**Purpose:** Verify the delivered system and run repeatable training scenarios.

This runbook describes the final 24-node, block-redundant topology. The March
2026 technical report is a milestone document and contains an earlier
PDU/rectifier design. Use this runbook and the project README for the delivered
architecture.

## Delivered System at a Glance

Each side contains 12 nodes:

| Stage | Side A | Side B |
|---|---|---|
| Utility | \`utility_a\` | \`utility_b\` |
| HV/MV transformer | \`hv_mv_transformer_a\` | \`hv_mv_transformer_b\` |
| MV switchgear | \`mv_switchgear_a\` | \`mv_switchgear_b\` |
| MV/LV transformer | \`mv_lv_transformer_a\` | \`mv_lv_transformer_b\` |
| LV switchgear and transfer point | \`lv_switchgear_a\` | \`lv_switchgear_b\` |
| Standby generator | \`generator_a\` | \`generator_b\` |
| UPS | \`ups_a\` | \`ups_b\` |
| Cooling fan bank | \`cooling_a\` | \`cooling_b\` |
| Four server racks | \`server_rack_a1\` through \`a4\` | \`server_rack_b1\` through \`b4\` |

Normal power path per side:

    utility -> HV/MV transformer -> MV switchgear -> MV/LV transformer
            -> LV switchgear -> UPS -> four server racks
                             \-> cooling

The generator is the secondary source into the LV switchgear. It is not an
additional in-series stage. Each rack is single-fed by its side's UPS.
Redundancy is at the side or block level, not at the rack level.

## As-Delivered Operating Boundaries

These constraints are part of the delivered system and should be stated before
testing:

1. The Raspberry Pi 5 onboard access point reliably supports about eight
   associated ESP32 stations. Use an external 2.4 GHz access point or adapter
   for a 24-node test. See \`deploy/EXTERNAL_AP.md\`.
2. \`scripts/setup_pi.sh\` installs the broker environment, but the Python
   simulation engine is not a systemd service. Start it manually for scenarios.
3. Utility state is operator-owned. A command sent to \`utility_a/control\` or
   \`utility_b/control\` is the most reliable software-triggered source event.
4. The broker publishes control to every non-utility node once per second. A
   one-shot manual status command to those nodes can be overwritten before the
   node's five-second telemetry update reaches the broker.
5. Generator startup is triggered by the corresponding utility's outage state.
   A downstream transformer or switchgear loss alone does not start it.
6. In the delivered engine, a transformer \`FAULT\` label does not force its
   broker-computed output voltage to zero. Remove power from the physical node
   when a real loss of that path is required.
7. The tracked Telegraf parser reliably stores numeric telemetry, but does not
   preserve JSON string fields such as \`state\` and \`status\`. The broker
   overview dashboard is a placeholder and the node dashboard is incomplete.
   Use OLED state and raw MQTT as primary evidence. Grafana is supplemental.
8. All displayed high voltages are simulated values. The tabletop hardware
   operates at safe low voltage.

## Run Record

Record the test context before starting:

| Item | Value |
|---|---|
| Date | |
| Operator | |
| Commit | |
| Access point | Pi onboard / external |
| Number of powered nodes | |
| Pi address | |
| Broker address | |
| Notes | |

## Part 1: Preflight

### 1. Physical and network setup

- [ ] Pi, baseplate, ESP32 boards, and OLEDs are visually intact.
- [ ] The access point is 2.4 GHz, channel 6, WPA-PSK.
- [ ] Use no more than eight nodes with the Pi onboard radio.
- [ ] Use the external-AP runbook for a full 24-node session.
- [ ] The Pi retains the broker address expected by the firmware.
- [ ] Every powered OLED progresses beyond its boot screen.

### 2. Pi service health

On the Pi:

    sudo systemctl status winter-river-hotspot
    sudo systemctl status mosquitto
    sudo systemctl status postgresql
    sudo systemctl status influxdb
    sudo systemctl status telegraf
    sudo systemctl status grafana-server

The hotspot service is only expected when the Pi itself is acting as the access
point. The monitoring services are optional for power-chain scenarios.

Run the project health helper:

    cd /home/$USER/ECE-26.1-Winter-River/scripts
    sudo ./status.sh

- [ ] Mosquitto is active.
- [ ] PostgreSQL is active.
- [ ] Expected clients appear in the DHCP or external-AP client list.
- [ ] Retained MQTT status is visible for each powered node.

### 3. Start the simulation engine

The engine must remain running in a dedicated terminal:

    cd /home/$USER/ECE-26.1-Winter-River
    cp -n broker/config.sample.toml broker/config.toml
    nano broker/config.toml
    source broker/venv/bin/activate
    python broker/main.py

- [ ] The process connects to MQTT.
- [ ] The process connects to PostgreSQL.
- [ ] Simulation ticks continue without exceptions.

Set the PostgreSQL password and any host-specific settings in
\`broker/config.toml\`. If the repository was installed somewhere else, use
that path instead.

### 4. Verify raw MQTT

In another terminal:

    mosquitto_sub -h 192.168.4.1 -t 'winter-river/#' -v

Each powered node should publish retained JSON on
\`winter-river/<node_id>/status\` about every five seconds. The broker publishes
\`/control\` for non-utility nodes about once per second.

- [ ] JSON is valid.
- [ ] Node IDs match the physical labels.
- [ ] Timestamps advance.
- [ ] No unexpected reconnect loop is visible.

### 5. Establish the normal baseline

Recover both utility inputs:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:GRID_OK VOLTAGE:230000'
    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_b/control -m 'STATUS:GRID_OK VOLTAGE:230000'

Restore cooling fan counts:

    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_a/control -m 'FANS_RUNNING:55'
    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_b/control -m 'FANS_RUNNING:55'

If a prior switchgear test left a sticky database state, restore it in
PostgreSQL:

    sudo -u postgres psql -d winter_river

    UPDATE live_status
       SET status_msg = 'CLOSED'
     WHERE node_id IN ('mv_switchgear_a', 'mv_switchgear_b');

    \q

Wait at least ten seconds.

- [ ] Both utilities report \`GRID_OK\`.
- [ ] Both MV switchgears report \`CLOSED\`.
- [ ] Both LV switchgears report \`CLOSED\`.
- [ ] Both generators report \`STANDBY\`.
- [ ] Both UPS nodes report \`NORMAL\` or \`CHARGING\`.
- [ ] Both cooling nodes report 55 running fans.
- [ ] Powered racks report \`NORMAL\`.
- [ ] \`winter-river/facility/status\` is present.

## Part 2: Software Verification

Run these checks from the repository root on a development machine.

### Python tests

    python -m pytest tests/

- [ ] All broker and thermal tests pass.

### ESP32 build matrix

    cd esp32-nodes
    pio run

- [ ] All 24 PlatformIO environments compile.

### Monitoring pipeline

Confirm that Telegraf receives MQTT and that numeric fields reach InfluxDB:

    journalctl -u telegraf -n 100 --no-pager

Open Grafana at \`http://<pi-address>:3000\`.

- [ ] Numeric measurements are arriving in the \`mqtt_metrics\` bucket.
- [ ] Grafana can connect to the provisioned InfluxDB datasource.
- [ ] Any visible numeric panels update.
- [ ] Operator records that state-string panels and complete 24-node coverage
  are not acceptance requirements for the delivered dashboard.

## Part 3: Reproducible Scenarios

For each scenario, watch both the OLEDs and raw MQTT. Do not rely on Grafana
alone.

### Scenario 1: Side A utility outage and generator transfer

Trigger:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:OUTAGE VOLTAGE:0'

Expected sequence:

1. \`utility_a\` reports \`OUTAGE\`.
2. Side A's utility-derived path loses power.
3. \`generator_a\` enters \`STARTING\`, then \`RUNNING\` after its configured
   delay.
4. \`ups_a\` carries the racks on battery during the gap.
5. \`lv_switchgear_a\` selects \`GENERATOR\`.
6. Side B remains normal.

Pass:

- [ ] Side A transfers without affecting Side B.
- [ ] The rack interruption, if any, matches the delivered model.
- [ ] The event is visible in raw MQTT and on the relevant OLEDs.

Recover:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:GRID_OK VOLTAGE:230000'

Wait for the primary path and normal states to return.

### Scenario 2: Utility sag without generator start

Trigger:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:SAG VOLTAGE:202400'

Expected:

- \`utility_a\` reports \`SAG\`.
- The generator remains in \`STANDBY\`.
- The side remains powered according to the simulation model.
- Side B is unchanged.

Recover with the \`GRID_OK\` command from Scenario 1.

### Scenario 3: Generator unavailable during utility loss

This scenario requires a physical action because a direct generator
\`STATUS:FAULT\` command is not durable in the delivered control loop.

Setup:

1. Confirm \`utility_a\` is \`GRID_OK\`.
2. Unplug or power off the \`generator_a\` ESP32.
3. Confirm its retained LWT reports \`OFFLINE\`.

Trigger the Side A utility outage from Scenario 1.

Expected:

- The LV switchgear has no secondary source.
- \`ups_a\` enters \`ON_BATTERY\`.
- Battery percentage falls by roughly one percentage point per simulation tick.
- After about 100 ticks from a full battery, the UPS and Side A racks lose
  support.
- Side B remains available.

Recover:

1. Return \`utility_a\` to \`GRID_OK\`.
2. Reconnect \`generator_a\`.
3. Wait for MQTT and normal states to recover.

### Scenario 4: One rack node removed

Setup: all nodes normal.

Action: unplug \`server_rack_a1\`.

Expected:

- Its MQTT LWT reports \`OFFLINE\`.
- The other three Side A racks remain normal.
- Side B remains normal.
- The result is a communication and physical-node loss, not a broker-generated
  rack \`FAULT\`.

Recover by reconnecting the board and waiting for telemetry.

### Scenario 5: MV switchgear trip

The broker database is the authoritative source for this non-utility state.
Apply the trip in PostgreSQL:

    sudo -u postgres psql -d winter_river

    UPDATE live_status
       SET status_msg = 'TRIPPED'
     WHERE node_id = 'mv_switchgear_a';

Expected:

- \`mv_switchgear_a\` remains \`TRIPPED\`.
- The downstream MV/LV transformer and primary LV path lose support.
- The upstream utility and HV/MV transformer remain available.
- The generator does not automatically start while \`utility_a\` remains
  \`GRID_OK\`.
- \`ups_a\` carries its racks on battery.
- Side B remains normal.

Recover:

    UPDATE live_status
       SET status_msg = 'CLOSED'
     WHERE node_id = 'mv_switchgear_a';

    \q

### Scenario 6: Transformer warning and physical path loss

To demonstrate a warning label:

    mosquitto_pub -h 192.168.4.1 -t winter-river/mv_lv_transformer_a/control -m 'STATUS:WARNING LOAD:95 TEMP:190'

Observe the OLED immediately. The broker may replace this command on its next
one-second control tick.

To demonstrate an actual transformer-path loss, unplug the transformer board and
confirm its LWT reports \`OFFLINE\`.

Expected:

- The physical removal is visible as \`OFFLINE\`.
- The path downstream of that transformer loses support.
- Side B remains normal.

Do not use a transformer \`FAULT\` label alone as proof that output voltage fell
to zero. That is not how the delivered engine computes transformer output.

Recover by reconnecting the board and waiting for telemetry.

### Scenario 7: Cooling capacity loss

Trigger:

    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_a/control -m 'FANS_RUNNING:0'
    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_b/control -m 'FANS_RUNNING:0'

Expected:

- Total running fans becomes zero.
- Facility airflow becomes zero.
- Thermal status becomes unavailable or faulted according to the model.
- Rack electrical power remains present; this is a thermal-capacity failure,
  not an immediate electrical outage.

Recover:

    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_a/control -m 'FANS_RUNNING:55'
    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_b/control -m 'FANS_RUNNING:55'

### Scenario 8: Simulation engine stopped

Action: stop \`broker/main.py\` with Ctrl+C while leaving MQTT and the boards
running.

Expected:

- ESP32 telemetry continues.
- MQTT remains available.
- Broker-generated control and facility updates stop.
- OLEDs retain their last local state until new commands or local fallback logic
  change it.

Recover by restarting the engine and confirming simulation ticks resume.

### Scenario 9: Access point loss

Action: briefly stop the active access point.

Expected:

- Nodes lose WiFi and MQTT.
- Mosquitto publishes retained LWT \`OFFLINE\` messages.
- Firmware retries with stagger and jitter.
- Nodes reconnect after the access point returns.

Recover the access point and allow time for the fleet to reassociate.

### Scenario 10: Both sides lose all sources

This is a deliberate full-outage test.

1. Physically power off or remove both generator nodes.
2. Command both utilities to \`OUTAGE\`.
3. Observe both UPS batteries discharge.

Expected:

- Both blocks eventually lose rack support after their UPS batteries deplete.
- The result is not described as a rack-level 2N transfer because no rack has a
  feed from the opposite side.

Recover both utilities to \`GRID_OK\`, reconnect both generators, and confirm the
normal baseline.

## Part 4: Recovery Verification

After every scenario:

- [ ] Utility nodes are \`GRID_OK\`.
- [ ] MV and LV switchgear states are no longer sticky test states.
- [ ] Generators return to \`STANDBY\`.
- [ ] UPS batteries recover toward full charge.
- [ ] Cooling fan counts return to 55 per side.
- [ ] All intended nodes reconnect to MQTT.
- [ ] Raw MQTT shows current telemetry, not only old retained values.
- [ ] The engine continues to tick without exceptions.

## Part 5: Instructor Review Questions

1. Where is the redundancy boundary in the delivered system?
2. Why does one rack not transfer to the other side's UPS?
3. What is the role of the LV switchgear?
4. Why is a utility command more durable than a direct generator fault command?
5. Why can an ESP32 report \`OFFLINE\` even when the simulated upstream source is
   healthy?
6. Why must full-fleet testing use an external access point?
7. Which evidence is authoritative for this delivery: OLED/raw MQTT or the
   incomplete dashboard?
8. Why does cooling loss not immediately remove electrical power from racks?

## Part 6: Acceptance Criteria

The delivered system passes this runbook when:

- [ ] Python tests pass.
- [ ] All 24 firmware environments compile.
- [ ] The tested node set connects within the access point's station limit.
- [ ] All 24 connect when a suitable external access point is used.
- [ ] Normal Side A and Side B power propagation is visible.
- [ ] Utility outage, generator transfer, UPS bridging, and recovery are
  repeatable.
- [ ] Physical node removal produces retained LWT \`OFFLINE\`.
- [ ] Cooling fan-count changes alter facility thermal output.
- [ ] Side-local scenarios do not incorrectly interrupt the opposite block.
- [ ] Operators can distinguish delivered constraints from planned or earlier
  architecture.

Incomplete Grafana panels, missing string-state fields, and absence of a broker
systemd unit are documented delivery boundaries. They should be recorded, but
are not silently treated as working features.

## Issue Log

| Time | Scenario | Node or service | Expected | Observed | Evidence | Resolution |
|---|---|---|---|---|---|---|
| | | | | | | |

## Final Cleanup

- [ ] Both utilities restored to \`GRID_OK\`.
- [ ] Both generators connected.
- [ ] Both cooling nodes restored to 55 fans.
- [ ] Any database-injected switchgear state restored to \`CLOSED\`.
- [ ] Simulation engine left in the intended running or stopped state.
- [ ] Test evidence and deviations saved with the run record.
