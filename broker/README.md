# Broker — Python Simulation Engine

`broker/main.py` is the **WinterRiverEngine** — the central simulation brain for ECE 26.1 Winter River. It connects to Mosquitto, subscribes to all node telemetry, runs a topology-aware cascade simulation at 1 Hz, and publishes computed power states back to every node via MQTT control commands. Optionally it also writes every tick to InfluxDB for Grafana visualisation.

---

## Architecture

```
ESP32 nodes
    │
    │  winter-river/<node_id>/status   (JSON telemetry, every 5s, retained)
    ▼
Mosquitto MQTT broker (192.168.4.1:1883)
    │
    │  paho-mqtt subscriber
    ▼
WinterRiverEngine (broker/main.py)
    ├── _load_topology()     PostgreSQL → in-memory node graph
    ├── _topo_sort()         Kahn's BFS — respects secondary_parent_id
    ├── _tick()              1 Hz cascade propagation loop
    │     ├── node type handlers (12 types)
    │     ├── publish control commands → MQTT
    │     └── write_to_influx() → InfluxDB (optional)
    └── PostgreSQL live_status updates
```

---

## Node Types Handled

| Type | node_ids | Key Logic |
|------|----------|-----------|
| `UTILITY` | `utility_a`, `utility_b` | Root nodes; `v_out` = 230 kV when `GRID_OK/SAG/SWELL`, 0 on `OUTAGE/FAULT/OFFLINE` |
| `HV_MV_TRANSFORMER` | `hv_mv_transformer_a/b` | 230 kV → 34.5 kV step-down; passes when `NORMAL/WARNING`, 0 on `FAULT` |
| `LV_SWITCHGEAR` | `lv_switchgear_a/b` | **Utility↔generator transfer point** (absorbed the ATS role). Primary = MV/LV transformer (utility) path → `CLOSED`; secondary = generator → `GENERATOR`; `NO_INPUT` when both dead (non-sticky); `OPEN/TRIPPED/FAULT` sticky. Output feeds UPS + cooling in parallel. |
| `MV_LV_TRANSFORMER` | `mv_lv_transformer_a/b` | 34.5 kV → 480 V; passes when `NORMAL/WARNING`, 0 on `FAULT` |
| `GENERATOR` | `generator_a`, `generator_b` | Standby while utility is live; 10-tick startup delay on utility loss; 480 V when `RUNNING`. Feeds the LV switchgear's secondary input. |
| `UPS` | `ups_a`, `ups_b` | Parent = lv_switchgear; passes voltage with battery tracking; feeds the side's 4 server_racks |
| `COOLING` | `cooling_a/b` | Parent = lv_switchgear (mech branch, rides the transfer). Fan bank (55 fans/side, 110 total) — drives broker thermal model |
| `SERVER_RACK` | `server_rack_a1..a4`, `server_rack_b1..b4` | Single-fed from this side's UPS; `NORMAL` when UPS is on grid or recharging (`CHARGING`), `DEGRADED` only while UPS is `ON_BATTERY`, `FAULT` when UPS is down. Side-A failure kills all 4 side-A racks. |

---

## Topological Sort

The engine uses **Kahn's BFS algorithm** so cascade propagation always visits parents before children — even with the dual-parent structure of the LV switchgear transfer point (primary = MV/LV transformer, secondary = generator).

```python
def _topo_sort(self, nodes):
    # Counts in-degree from BOTH parent_id and secondary_parent_id
    # UTILITY nodes are enqueued before GENERATOR nodes so generator
    # can correctly check whether utility is still live.
```

---

## Generator Startup Delay

When utility power is lost the generator does not supply power instantly. The engine simulates a realistic 10-tick (~10 second) startup sequence:

```
Utility lost  → gen_timer = GEN_STARTUP_TICKS (10)
Each tick:    → gen_timer decrements, state = STARTING, v_out = 0
Timer = 0     → state = RUNNING, v_out = 480 V
```

This means the LV switchgear output drops to 0 V (`NO_INPUT`) for ~10 seconds before it transfers to the generator (`GENERATOR`) — exactly matching a real data centre emergency scenario where the UPS must carry the load during the gap.

---

## Block-Redundant 2N

Side A and Side B are two fully independent power chains. There is no shared
rectifier — each side feeds its own 4 server racks single-sided. Redundancy
is at the *block* level: if Side A loses utility AND its generator fails,
all 4 side-A racks go FAULT; Side B continues unaffected.

Each side is "up" iff its UPS (`ups_a` / `ups_b`) is present and producing
voltage — there is no shared convergence point to roll the two sides together.

---

## Installation

```bash
cd broker
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

## Configuration

Copy the template and edit:

```bash
cp config.sample.toml config.toml
```

`broker/config.toml` is git-ignored (developer-local). Minimum required sections:

```toml
[mqtt]
broker_host = "192.168.4.1"   # use "localhost" if running on the Pi itself
broker_port = 1883
keepalive   = 60

[database]
dsn = "host=localhost dbname=winter_river user=postgres password=your_password"

[simulation]
tick_rate = 1.0    # seconds per simulation tick
```

If `python main.py` exits with “broker config not found,” run the copy command
above on the Pi. Fresh clones do not include `broker/config.toml` because it can
contain local passwords.

**Optional** — add this section to enable InfluxDB writes:

```toml
[influxdb]
url    = "http://localhost:8086"
token_env = "INFLUXDB_TOKEN"          # preferred; also checks INFLUX_TOKEN / INFLUXDB_ADMIN_TOKEN
# token = "replace_with_real_influxdb_token"
org    = "iot-project"
bucket = "mqtt_metrics"
```

If `[influxdb]` is absent, no token is found, or `influxdb-client` is not installed, the engine runs in MQTT-only mode without error. On a Pi provisioned with `scripts/setup_pi.sh`, set `INFLUXDB_TOKEN` from the same token used for Grafana/Telegraf or replace `token` with the real InfluxDB token; leaving the sample token in place against an already-initialized InfluxDB will produce `401 unauthorized` writes.

---

## Running

```bash
cd broker
source venv/bin/activate
python main.py
```

The engine will:
1. Connect to MQTT broker at the configured host
2. Subscribe to `winter-river/#`
3. Load topology from PostgreSQL (`nodes` + `live_status` tables)
4. Begin a 1 Hz simulation tick loop
5. Publish control commands to each node's `/control` topic every tick

---

## Database Schema

Run `scripts/init_db.sql` to initialise:

```bash
psql -U postgres -d winter_river -f ../scripts/init_db.sql
```

Key tables:

```sql
nodes (
    node_id              VARCHAR(50) PRIMARY KEY,
    node_type            VARCHAR(30),     -- UTILITY, LV_SWITCHGEAR, GENERATOR, UPS, etc.
    side                 CHAR(1),         -- 'A' or 'B' (no shared nodes)
    parent_id            VARCHAR(50),     -- primary upstream node
    secondary_parent_id  VARCHAR(50),     -- LV switchgear's generator input
    rated_voltage        FLOAT,
    v_ratio              FLOAT
)

live_status (
    node_id       VARCHAR(50) PRIMARY KEY,
    is_present    BOOLEAN,    -- true when node is publishing telemetry
    v_in          FLOAT,      -- input voltage computed by engine
    v_out         FLOAT,      -- output voltage published to node
    status_msg    VARCHAR(50),
    battery_level INT,        -- UPS only
    gen_timer     INT,        -- GENERATOR startup countdown ticks
    last_update   TIMESTAMP
)
```

---

## MQTT Topics

| Direction | Topic pattern | Content |
|-----------|---------------|---------|
| Inbound | `winter-river/<node_id>/status` | JSON telemetry (retained, every 5s) |
| Inbound | `winter-river/weather/control` | Operator weather commands (non-retained), e.g. `PRESET:4` |
| Outbound | `winter-river/<node_id>/control` | Space-delimited commands, e.g. `INPUT:480.0 STATUS:NORMAL` |
| Outbound | `winter-river/facility/status` | Computed thermal/PUE state (retained, every tick) |
| Outbound | `winter-river/weather/status` | Active outdoor conditions feeding the thermal model (retained, every tick) |

Full command reference: see each component's README in `esp32-nodes/src/<type>/README.md`.

### Weather control

The thermal model's outdoor weather can be changed at runtime over MQTT. The
broker **always boots at preset 1 (Virginia Summer)** for deterministic startup;
weather is no longer read from `config.toml`. Publish to `winter-river/weather/control`
to change it — the broker republishes `weather/status` immediately and the next
tick recomputes facility temps from the new weather.

| Token | Effect |
|-------|--------|
| `PRESET:<1-6>` | Select a preset, clearing any custom overrides |
| `RESET` | Return to preset 1 (the startup default) |
| `OUTDOOR_F:<f>` | Override outdoor dry-bulb temperature (°F) |
| `RH_PCT:<f>` | Override relative humidity (clamped 0–100) |

Presets: `1` Virginia Summer · `2` Eastern Oregon Winter · `3` Ohio Spring ·
`4` Arizona Summer · `5` Stockholm Winter · `6` Singapore Monsoon.

```bash
mosquitto_pub -h 192.168.4.1 -t "winter-river/weather/control" -m "PRESET:4"
mosquitto_pub -h 192.168.4.1 -t "winter-river/weather/control" -m "PRESET:6 RH_PCT:75"
mosquitto_pub -h 192.168.4.1 -t "winter-river/weather/control" -m "RESET"
```

Tokens are space-delimited and applied in order, so a compound command selects a
preset then overrides one field (the `weather/status` payload then carries
`"custom": true`). Invalid commands are logged and ignored — the current weather
is left unchanged. Commands must be **non-retained** (retained messages are
ignored) so a restart truly returns to preset 1; runtime weather is never persisted.

---

## Development Tools

```bash
pip install -r requirements-dev.txt

# Format
black main.py

# Lint
flake8 main.py

# Type check
mypy main.py

# Tests (stubs — add to tests/)
pytest
```
