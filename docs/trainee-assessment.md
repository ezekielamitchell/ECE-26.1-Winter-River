# Winter River Trainee Assessment

**Audience:** Data center trainees and new operators

**Format:** Hands-on workshop assessment

**Time:** About 30 minutes

**Passing score:** 16 of 20 points

This assessment matches the final 24-node system delivered in June 2026.
Displayed utility and distribution voltages are simulated values. The tabletop
hardware itself operates at safe low voltage.

> A full 24-node workshop requires an external 2.4 GHz access point. The
> Raspberry Pi 5 onboard access point reliably supports only about eight ESP32
> stations at once.

## Learning Objectives

By the end of the workshop, the trainee should be able to:

- identify the major stages in one power block;
- explain the separate roles of switchgear, generators, UPS units, and cooling;
- trace power from utility input to a server rack;
- explain block-level redundancy between Side A and Side B;
- interpret a utility outage, a missing generator, and a cooling-capacity loss;
- use OLED and MQTT state as primary evidence.

## Student Worksheet

Name: ______________________________

Date: ______________________________

Instructor: _________________________

### Part 1: Match the Equipment to Its Job, 5 points

Write the correct letter next to each equipment group. Any five correct answers
earn the full five points.

| Equipment | Answer |
|---|---|
| 1. Utility | |
| 2. Transformers | |
| 3. Switchgear | |
| 4. Generator | |
| 5. UPS | |
| 6. Cooling | |
| 7. Server racks | |

**Jobs**

A. Uses battery energy to bridge a short loss of input power

B. Represents the incoming electric grid

C. Changes voltage between distribution levels

D. Houses the simulated computing load

E. Opens, closes, protects, and routes a distribution bus

F. Provides standby 480 V power when the utility source is lost

G. Removes heat by operating a simulated bank of fans

### Part 2: Trace the Normal Power Path, 5 points

Number these seven stages from 1 through 7 in the order that normal utility
power reaches a server rack:

| Stage | Order |
|---|---|
| MV switchgear | |
| UPS | |
| Utility | |
| Server rack | |
| LV switchgear | |
| MV/LV transformer | |
| HV/MV transformer | |

Where does the generator connect during an outage?

____________________________________________________________________

### Part 3: Explain Redundancy, 3 points

1. If all of Side A loses power, what should happen to Side B?

____________________________________________________________________

2. Does \`server_rack_a1\` have a second feed from \`ups_b\`?

____________________________________________________________________

3. Is the system's redundancy at the individual-rack level or at the complete
side/block level?

____________________________________________________________________

### Part 4: Observe Three Demonstrations, 6 points

For each demonstration, write what changed and what remained available.

#### Demonstration 1: Side A utility outage

What happened to \`generator_a\`, \`lv_switchgear_a\`, and \`ups_a\`?

____________________________________________________________________

What happened to Side B?

____________________________________________________________________

#### Demonstration 2: Utility loss with the Side A generator unavailable

What carried the Side A racks immediately after the outage?

____________________________________________________________________

What happened after the UPS battery depleted?

____________________________________________________________________

#### Demonstration 3: Cooling fan loss

Did the racks immediately lose electrical power?

____________________________________________________________________

What risk increased when airflow fell to zero?

____________________________________________________________________

### Part 5: One-Sentence Summary, 1 point

In one sentence, describe what Winter River teaches:

____________________________________________________________________

____________________________________________________________________

## Instructor Key

### Part 1

1. Utility: B
2. Transformers: C
3. Switchgear: E
4. Generator: F
5. UPS: A
6. Cooling: G
7. Server racks: D

Award one point per correct match, up to five points.

### Part 2

Correct normal path:

1. Utility
2. HV/MV transformer
3. MV switchgear
4. MV/LV transformer
5. LV switchgear
6. UPS
7. Server rack

The generator is a backup branch into the LV switchgear's secondary input. It is
not an additional stage after the LV switchgear or UPS.

Award four points for the correct sequence and one point for placing the
generator at the LV switchgear backup input.

### Part 3

1. Side B should continue operating.
2. No. Each rack is single-fed from its own side's UPS.
3. Redundancy is at the side or block level.

### Part 4

**Demonstration 1:** \`generator_a\` should move from \`STANDBY\` to
\`STARTING\` and then \`RUNNING\`. \`ups_a\` bridges the start delay on battery.
\`lv_switchgear_a\` selects \`GENERATOR\`. Side B remains normal.

**Demonstration 2:** the Side A UPS initially supplies the racks from its
battery. With the generator physically unavailable and utility still lost, the
battery falls by roughly one percentage point per simulation tick. Starting
from full charge, allow about 100 seconds for depletion. Side A racks then lose
support while Side B remains available.

**Demonstration 3:** rack electrical power remains present. Cooling capacity and
airflow are lost, so thermal risk increases. The model may mark facility thermal
output unavailable or faulted when no fans are running.

### Part 5

Accept a sentence that describes learning power-chain topology, block
redundancy, or failure response through a physical simulator.

## Instructor Setup

### Before trainees arrive

- Use an external 2.4 GHz access point for all 24 nodes, or limit the workshop to
  eight nodes on the Pi onboard radio.
- Start Mosquitto and PostgreSQL.
- Copy \`broker/config.sample.toml\` to \`broker/config.toml\` and set the local
  database password if this Pi has not been configured before.
- From the repository root, activate \`broker/venv\` and start
  \`broker/main.py\` manually in a dedicated terminal. It is not installed as a
  systemd service.
- Subscribe to \`winter-river/#\` in a second terminal.
- Confirm both utilities are \`GRID_OK\`.
- Confirm both generators are connected and initially \`STANDBY\`.
- Confirm both cooling nodes report 55 running fans.
- Treat OLED state and raw MQTT as primary evidence. The delivered Grafana
  dashboards have incomplete node coverage and do not reliably retain string
  state fields.

Suggested MQTT monitor:

    mosquitto_sub -h 192.168.4.1 -t 'winter-river/#' -v

### Demonstration 1: Side A utility outage

Trigger:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:OUTAGE VOLTAGE:0'

Allow trainees to observe the utility state, generator startup delay, UPS
battery bridge, and LV switchgear transfer.

Recover:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:GRID_OK VOLTAGE:230000'

Wait for the primary path and normal states to return.

### Demonstration 2: Generator unavailable

Do not use a direct \`generator_a STATUS:FAULT\` command. The broker controls
non-utility state every second and can overwrite it.

1. Confirm Side A is normal.
2. Physically unplug or power off the \`generator_a\` board.
3. Confirm its retained MQTT LWT reports \`OFFLINE\`.
4. Trigger the Side A utility outage:

       mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:OUTAGE VOLTAGE:0'

5. Observe \`ups_a\` and the Side A racks. Allow about 100 simulation ticks for a
   fully charged UPS to deplete.

Recover the utility first, then reconnect the generator:

    mosquitto_pub -h 192.168.4.1 -t winter-river/utility_a/control -m 'STATUS:GRID_OK VOLTAGE:230000'

### Demonstration 3: Cooling fan loss

Trigger:

    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_a/control -m 'FANS_RUNNING:0'
    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_b/control -m 'FANS_RUNNING:0'

Recover:

    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_a/control -m 'FANS_RUNNING:55'
    mosquitto_pub -h 192.168.4.1 -t winter-river/cooling_b/control -m 'FANS_RUNNING:55'

Explain that loss of cooling capacity creates thermal risk but does not
immediately remove rack electrical power.

## Scoring

| Section | Points |
|---|---:|
| Part 1: Equipment roles | 5 |
| Part 2: Power path | 5 |
| Part 3: Redundancy | 3 |
| Part 4: Demonstrations | 6 |
| Part 5: Summary | 1 |
| **Total** | **20** |

Recommended interpretation:

- 18 to 20: strong understanding
- 16 to 17: meets workshop objective
- 13 to 15: review the power path and failure sequence
- 0 to 12: repeat the guided demonstration
