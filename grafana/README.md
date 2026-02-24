# Grafana Visualization Setup

Grafana is installed directly on the Raspberry Pi via apt (no Docker).
It queries PostgreSQL directly for live and historical node data.

## Architecture

```
ESP32 → MQTT → Mosquitto → broker/main.py → PostgreSQL ← Grafana
```

## Directory Structure

```
grafana/
├── README.md
├── grafana.ini                        # Grafana server config
├── dashboards/                        # Dashboard JSON exports
└── provisioning/
    ├── dashboards/
    │   └── dashboard.yml              # Auto-loads dashboards from dashboards/
    └── datasources/
        └── datasource.yml             # PostgreSQL datasource (auto-provisioned)
```

## Setup

Grafana and PostgreSQL are installed automatically by `scripts/setup_pi.sh`.

After setup:
- Grafana runs at `http://192.168.4.1:3000`
- Default login: `admin` / `admin` (change on first login)
- PostgreSQL datasource is auto-provisioned from `provisioning/datasources/datasource.yml`

## PostgreSQL Tables Available

| Table | Contents |
|---|---|
| `live_status` | Current digital twin state for all 16 nodes |
| `historical_data` | Full telemetry history with JSON metrics |
| `nodes` | Static topology (node type, side, parent, v_ratio) |

## Example Grafana Queries

**Live node status:**
```sql
SELECT node_id, status_msg, v_out, is_present, last_update
FROM live_status
ORDER BY node_id
```

**Historical voltage for a node:**
```sql
SELECT timestamp, metrics->>'v_out' AS v_out
FROM historical_data
WHERE node_id = '$node_id'
ORDER BY timestamp DESC
LIMIT 100
```

## Datasource Credentials

The datasource provisioning file uses a `grafana_reader` PostgreSQL user.
`setup_pi.sh` creates this user with read-only access to `winter_river`.
Update the password in `provisioning/datasources/datasource.yml` before deploying.
