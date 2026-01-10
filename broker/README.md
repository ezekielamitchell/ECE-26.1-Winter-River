# MQTT Broker

Python-based MQTT broker for the ECE-26.1 IoT Environmental Monitoring System.

## Features

- MQTT protocol support for ESP32 sensor nodes
- PostgreSQL database for persistent storage
- Real-time data processing and validation
- Prometheus metrics endpoint
- Configurable via TOML configuration file
- Structured logging with structlog

## Installation

### Prerequisites

- Python 3.9 or higher
- PostgreSQL 12 or higher

### Setup

1. Create a virtual environment:

```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

2. Install dependencies:

```bash
pip install -r requirements.txt
```

3. Install development dependencies (optional):

```bash
pip install -r requirements-dev.txt
```

4. Configure the broker:

```bash
cp config.sample.toml config.toml
# Edit config.toml with your settings
```

5. Initialize the database:

```bash
alembic upgrade head
```

## Configuration

Edit `config.toml` to configure the broker:

```toml
[mqtt]
host = "0.0.0.0"
port = 1883
keepalive = 60
client_id = "winter-river-broker"

[database]
host = "localhost"
port = 5432
database = "sensor_data"
user = "postgres"
password = "your_password"

[logging]
level = "INFO"
format = "json"

[metrics]
enabled = true
port = 9090
```

## Running

### Development

```bash
python main.py
```

### Production

Use systemd service (see deployment documentation):

```bash
sudo systemctl start mqtt-broker
sudo systemctl enable mqtt-broker
```

## Development

### Running Tests

```bash
# All tests
pytest

# With coverage
pytest --cov=src --cov-report=html

# Specific test file
pytest tests/test_broker.py

# With verbose output
pytest -v
```

### Code Formatting

```bash
# Format code
black src/ tests/

# Sort imports
isort src/ tests/

# Check style
flake8 src/ tests/

# Type checking
mypy src/
```

### Linting

```bash
pylint src/
```

## Project Structure

```text
broker/
├── main.py              # Entry point
├── config.toml          # Configuration
├── requirements.txt     # Dependencies
├── pyproject.toml       # Project metadata
├── src/                 # Source code
│   ├── __init__.py
│   ├── broker.py        # MQTT broker logic
│   ├── database.py      # Database interface
│   ├── models.py        # Data models
│   └── utils.py         # Utilities
├── tests/               # Test suite
│   ├── __init__.py
│   ├── test_broker.py
│   ├── test_database.py
│   └── conftest.py
└── alembic/             # Database migrations
    ├── versions/
    └── env.py
```

## Architecture

### Components

1. **MQTT Handler**: Receives messages from ESP32 nodes
2. **Data Validator**: Validates sensor readings
3. **Database Writer**: Persists data to PostgreSQL
4. **Metrics Exporter**: Exposes Prometheus metrics
5. **Logger**: Structured logging for debugging

### Data Flow

```text
ESP32 → MQTT → Validator → Database → Grafana
                    ↓
                Metrics → Prometheus
```

## MQTT Topics

- `sensor/{node_id}/temperature` - Temperature readings (°C)
- `sensor/{node_id}/humidity` - Humidity readings (%)
- `sensor/{node_id}/pressure` - Pressure readings (hPa)
- `sensor/{node_id}/status` - Node status messages

## Database Schema

```sql
CREATE TABLE sensor_readings (
    id SERIAL PRIMARY KEY,
    node_id VARCHAR(50) NOT NULL,
    sensor_type VARCHAR(50) NOT NULL,
    value FLOAT NOT NULL,
    timestamp TIMESTAMP NOT NULL DEFAULT NOW(),
    metadata JSONB
);

CREATE INDEX idx_node_timestamp ON sensor_readings(node_id, timestamp DESC);
```

## Metrics

Exposed on port 9090 (configurable):

- `sensor_messages_total` - Total messages received per node
- `sensor_messages_invalid` - Invalid messages received
- `database_writes_total` - Total database writes
- `database_write_errors` - Database write errors
- `broker_uptime_seconds` - Broker uptime

## Logging

Structured JSON logs with fields:

- `timestamp` - ISO 8601 timestamp
- `level` - Log level (DEBUG, INFO, WARNING, ERROR)
- `event` - Event description
- `node_id` - Related node ID (if applicable)
- `duration` - Operation duration (if applicable)

Example:

```json
{
  "timestamp": "2026-01-09T16:45:23.123Z",
  "level": "INFO",
  "event": "message_received",
  "node_id": "node1",
  "topic": "sensor/node1/temperature",
  "value": 23.5
}
```

## Troubleshooting

### Connection Issues

- Verify MQTT broker is running: `sudo systemctl status mqtt-broker`
- Check firewall rules allow port 1883
- Verify ESP32 nodes can reach the broker IP

### Database Issues

- Check PostgreSQL is running: `sudo systemctl status postgresql`
- Verify database credentials in config.toml
- Check database logs: `sudo journalctl -u postgresql -f`

### Performance Issues

- Monitor metrics at `http://broker-ip:9090/metrics`
- Check database query performance
- Adjust PostgreSQL connection pool settings

## License

MIT License - see LICENSE file for details
