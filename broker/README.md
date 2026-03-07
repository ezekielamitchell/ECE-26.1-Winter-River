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
    │     ├── node type handlers (13 types)
    │     ├── publish control commands → MQTT
    │     └── write_to_influx() → InfluxDB (optional)
    └── PostgreSQL live_status updates
```

---

## Node Types Handled

| Type | node_ids | Key Logic |
|------|----------|-----------|
| `UTILITY` | `utility_a`, `utility_b` | Root nodes; `v_out` = 230 kV when `GRID_OK/SAG/SWELL`, 0 on `OUTAGE/FAULT/OFFLINE` |
| `MV_SWITCHGEAR` | `mv_switchgear_a/b` | Passes parent voltage when `CLOSED`; 0 when `OPEN/TRIPPED/FAULT` |
| `MV_LV_TRANSFORMER` | `mv_lv_transformer_a/b` | Passes parent voltage when `NORMAL/WARNING`; 0 when `FAULT` |
| `GENERATOR` | `generator_a`, `generator_b` | Standby while utility is live; 10-tick startup delay on utility loss; 480 V when `RUNNING` |
| `ATS` | `ats_a`, `ats_b` | Prefers transformer (utility) path; falls back to generator; `OPEN` if both down |
| `LV_DIST` | `lv_dist_a/b` | Distributes ATS voltage to all downstream IT + mechanical branches |
| `UPS` | `ups_a`, `ups_b` | Passes voltage with battery tracking; 0 V on `FAULT` |
| `PDU` | `pdu_a`, `pdu_b` | Passes UPS voltage; 0 V on `FAULT` |
| `RECTIFIER` | `rectifier_a/b` | 480 V AC → 48 V DC; 0 on `OFF`/`FAULT` |
| `COOLING` | `cooling_a/b` | Mechanical load — monitored but not in IT power chain |
| `LIGHTING` | `lighting_a/b` | Mechanical load — monitored but not in IT power chain |
| `MONITORING` | `monitoring_a/b` | 120 V monitoring load — not in IT power chain |
| `SERVER_RACK` | `server_rack` | 2N node — NORMAL if both paths live, DEGRADED if one path down, FAULT if both down |

---

## Topological Sort

The engine uses **Kahn's BFS algorithm** so cascade propagation always visits parents before children — even with the dual-parent structure required by ATS and the 2N server rack.

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

This means the ATS output drops to 0 V for ~10 seconds before generator power arrives — exactly matching a real data centre emergency scenario where the UPS must carry the load during the gap.

---

## 2N Server Rack Logic

`server_rack` has two parents (`rectifier_a` and `rectifier_b`) and combines their status:

| Path A | Path B | server_rack state | v_out |
|--------|--------|-------------------|-------|
| live   | live   | `NORMAL`          | 48 V  |
| live   | dead   | `DEGRADED`        | 48 V  |
| dead   | live   | `DEGRADED`        | 48 V  |
| dead   | dead   | `FAULT`           | 0 V   |

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
cp config.sample.toml ../config.toml
```

`config.toml` is git-ignored. Minimum required sections:

```toml
[mqtt]
host      = "192.168.4.1"
port      = 1883
keepalive = 60

[database]
host     = "localhost"
port     = 5432
database = "sensor_data"
user     = "postgres"
password = "your_password"

[simulation]
tick_rate = 1.0    # seconds per simulation tick
```

**Optional** — add this section to enable InfluxDB writes:

```toml
[influxdb]
url    = "http://localhost:8086"
token  = "my-super-secret-auth-token"
org    = "iot-project"
bucket = "mqtt_metrics"
```

If `[influxdb]` is absent or `influxdb-client` is not installed, the engine runs in MQTT-only mode without error.

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
psql -U postgres -d sensor_data -f scripts/init_db.sql
```

Key tables:

```sql
nodes (
    node_id              VARCHAR(50) PRIMARY KEY,
    node_type            VARCHAR(30),     -- UTILITY, MV_SWITCHGEAR, GENERATOR, ATS, etc.
    side                 CHAR(1),         -- 'A', 'B', or NULL (shared)
    parent_id            VARCHAR(50),     -- primary upstream node
    secondary_parent_id  VARCHAR(50),     -- ATS generator input / server_rack Side B
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
| Outbound | `winter-river/<node_id>/control` | Space-delimited commands, e.g. `INPUT:480.0 STATUS:NORMAL` |

Full command reference: see each component's README in `esp32-nodes/src/<type>/README.md`.

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
