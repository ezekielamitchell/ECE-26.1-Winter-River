"""
Winter River Simulation Engine
Reads ESP32 MQTT telemetry → propagates power hierarchy → publishes control commands.
Topology: Side A + Side B are two fully independent block-redundant chains.
Each side feeds 4 server_racks single-sided (no shared rectifier, no rack-level
2N). Side-A failure kills all 4 of side-A's racks; side-B continues.
Chain per side: utility → hv_mv_xfmr → mv_switchgear (34.5 kV) → mv_lv_xfmr
→ lv_switchgear (480 V LV transfer point); generator ↗ lv_switchgear → ups
→ server_rack_{1..4}; lv_switchgear ↘ cooling. The LV switchgear is the
utility↔generator transfer point (no separate ATS node).
24 active nodes total (12 per side). Tick rate: 1 Hz (configurable in config.toml).
"""

import json
import logging
import math
import os
import time
from collections import defaultdict
from datetime import timedelta

import paho.mqtt.client as mqtt
import psycopg2
import toml
from psycopg2.extras import RealDictCursor

from thermal import WEATHER_PRESETS, ThermalConfig, compute_thermal, resolve_weather

try:
    from influxdb_client import InfluxDBClient, Point
    from influxdb_client.client.write_api import SYNCHRONOUS
    HAS_INFLUX = True
except ImportError:
    HAS_INFLUX = False

# ── CONFIGURATION ─────────────────────────────────────────────────────────────

_BROKER_DIR = os.path.abspath(os.path.dirname(__file__))
_DEFAULT_CFG_PATH = os.path.join(_BROKER_DIR, "config.toml")
_SAMPLE_CFG_PATH = os.path.join(_BROKER_DIR, "config.sample.toml")


def _load_config():
    cfg_path = os.environ.get("WINTER_RIVER_CONFIG", _DEFAULT_CFG_PATH)
    if not os.path.exists(cfg_path):
        raise SystemExit(
            "Winter River broker config not found:\n"
            f"  {cfg_path}\n\n"
            "Create a local config from the tracked sample, then set the "
            "PostgreSQL password/DSN for this Pi:\n"
            f"  cd {_BROKER_DIR}\n"
            "  cp config.sample.toml config.toml\n"
            "  nano config.toml\n\n"
            "broker/config.toml is intentionally git-ignored so each Pi keeps "
            "its own local credentials."
        )

    try:
        cfg = toml.load(cfg_path)
    except toml.TomlDecodeError as exc:
        raise SystemExit(f"Invalid TOML in {cfg_path}: {exc}") from exc

    required_keys = (
        ("mqtt", "broker_host"),
        ("mqtt", "broker_port"),
        ("database", "dsn"),
    )
    missing = []
    for section_name, key in required_keys:
        section = cfg.get(section_name)
        if not isinstance(section, dict) or key not in section:
            missing.append(f"{section_name}.{key}")

    if missing:
        raise SystemExit(
            "Winter River broker config is missing required key(s): "
            f"{', '.join(missing)}\n"
            f"Check {cfg_path} against the sample at {_SAMPLE_CFG_PATH}."
        )

    return cfg


_cfg = _load_config()

MQTT_BROKER = _cfg["mqtt"]["broker_host"]
MQTT_PORT   = _cfg["mqtt"]["broker_port"]
DB_CONFIG   = _cfg["database"]["dsn"]
TICK_RATE   = _cfg.get("simulation", {}).get("tick_rate", 1.0)

# Generator startup delay in simulation ticks (1 tick = 1 s at default tick rate)
GEN_STARTUP_TICKS = 10

# Weather starts deterministically at this preset on every broker boot. Runtime
# changes arrive over MQTT (winter-river/weather/control) and are never persisted
# — a restart always returns here regardless of the last command. The TOML
# [weather] block is no longer read at startup; thermal sizing stays in [thermal].
# See _handle_weather_control.
DEFAULT_WEATHER_PRESET = 1

# Status topics published by the broker for Telegraf/Grafana. They intentionally
# are not rows in the DB topology and must not be treated as ESP32 telemetry.
VIRTUAL_STATUS_NODE_IDS = {"facility", "weather"}

# Common environment variable names used by setup scripts and service defaults.
INFLUX_TOKEN_ENV_VARS = ("INFLUXDB_TOKEN", "INFLUX_TOKEN", "INFLUXDB_ADMIN_TOKEN")

# Mark a node OFFLINE if no telemetry arrives in this window. Covers silent
# ESP32 hangs that don't fire the MQTT LWT — telemetry interval is 5 s, so
# 3 missed cycles + slack catches a hung node without flapping under jitter.
STALE_NODE_THRESHOLD_SEC = 20

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("winter-river")


def _resolve_influx_token(icfg):
    """Resolve an InfluxDB token from env first, then config fallback."""
    configured = icfg.get("token_env")
    env_names = []
    if isinstance(configured, str):
        env_names.append(configured)
    elif isinstance(configured, (list, tuple)):
        env_names.extend(str(name) for name in configured)
    env_names.extend(INFLUX_TOKEN_ENV_VARS)

    seen = set()
    for name in env_names:
        if not name or name in seen:
            continue
        seen.add(name)
        token = os.environ.get(name)
        if token:
            return token

    return icfg.get("token")


# ── ENGINE ────────────────────────────────────────────────────────────────────

class WinterRiverEngine:
    def __init__(self):
        # DB is optional. Without it the broker still boots and runs the MQTT
        # client, but on_message drops every telemetry message (no node_id
        # validation possible) and run_simulation_tick early-returns. Useful
        # for previewing on a dev machine that doesn't have Postgres running.
        try:
            self.db = psycopg2.connect(DB_CONFIG, cursor_factory=RealDictCursor)
            log.info("PostgreSQL connected")
        except psycopg2.OperationalError as exc:
            log.warning(
                "PostgreSQL unavailable (%s) — broker running in no-DB mode; "
                "telemetry will be ignored and simulation ticks will no-op",
                exc,
            )
            self.db = None

        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_message = self.on_message
        self.mqtt_client.on_connect = self._on_mqtt_connect
        # connect_async + loop_start lets the broker boot even if Mosquitto is
        # not reachable yet; paho's background thread will keep retrying.
        try:
            self.mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
            self.mqtt_client.loop_start()
            log.info("MQTT connecting to %s:%d (async)", MQTT_BROKER, MQTT_PORT)
        except Exception as exc:
            log.warning("MQTT setup failed (%s) — will retry in background", exc)

        self._thermal_cfg = ThermalConfig.from_mapping(_cfg.get("thermal"))
        # Always boot at the default preset (not config-driven) so every broker
        # start is deterministic; operators change weather at runtime via MQTT
        # (winter-river/weather/control → _handle_weather_control).
        self._weather     = resolve_weather({"preset": DEFAULT_WEATHER_PRESET})
        self._latest_thermal = None
        self._facility_metrics_disabled = False

        # Static topology cache — populated from `nodes` at startup. The set is
        # rebuilt on a cache miss in on_message so re-running init_db.sql
        # mid-session takes effect without a broker restart. Avoids one SELECT
        # per inbound telemetry packet.
        self._known_nodes = self._load_known_nodes()

        # Live fan-bank counts reported by cooling_a / cooling_b telemetry.
        # Default = nominal so the first tick (before any telemetry arrives)
        # has sane values; on_message keeps these in sync from MQTT.
        per_side_nominal = self._thermal_cfg.fans_per_module
        self._cooling_fans = {
            "cooling_a": per_side_nominal,
            "cooling_b": per_side_nominal,
        }
        log.info(
            "Thermal model: weather=%s (%.1f F / %.0f%% RH), modules=std:%d stor:%d ai:%d, fan_modules=%d",
            self._weather.get("name"), self._weather["outdoor_f"], self._weather["rh_pct"],
            self._thermal_cfg.standard_modules, self._thermal_cfg.storage_modules,
            self._thermal_cfg.ai_modules, self._thermal_cfg.fan_modules,
        )

        # Optional InfluxDB direct writes (computed state, 1 Hz)
        self._influx_write_api = None
        self._influx_bucket    = None
        if HAS_INFLUX and "influxdb" in _cfg:
            icfg = _cfg["influxdb"]
            token = _resolve_influx_token(icfg)
            if not token:
                log.warning(
                    "InfluxDB config present but no token found; direct broker "
                    "InfluxDB writes disabled"
                )
            else:
                try:
                    self._influx_client  = InfluxDBClient(
                        url=icfg["url"], token=token, org=icfg["org"]
                    )
                    self._influx_write_api = self._influx_client.write_api(
                        write_options=SYNCHRONOUS
                    )
                    self._influx_bucket = icfg["bucket"]
                    log.info("InfluxDB connected to %s", icfg["url"])
                except Exception as exc:
                    log.warning("InfluxDB init failed (continuing without): %s", exc)

        log.info("Winter River Engine initialised")

    # ── MQTT lifecycle ────────────────────────────────────────────────────────

    def _on_mqtt_connect(self, client, userdata, flags, rc):
        if rc == 0:
            log.info("MQTT connected to %s:%d", MQTT_BROKER, MQTT_PORT)
            client.subscribe("winter-river/+/status", qos=1)
            # Operator weather control (thermal-only; weather is not a DB node).
            client.subscribe("winter-river/weather/control", qos=1)
        else:
            log.warning("MQTT connect failed (rc=%d) — will retry", rc)

    # ── MQTT ingestion ────────────────────────────────────────────────────────

    def _load_known_nodes(self):
        """Return the set of seeded node_ids. Empty set in no-DB mode."""
        if self.db is None:
            return set()
        try:
            with self.db.cursor() as cur:
                cur.execute("SELECT node_id FROM nodes")
                ids = {row["node_id"] for row in cur.fetchall()}
            log.info("Topology cache loaded: %d nodes", len(ids))
            return ids
        except Exception as exc:
            log.warning("Failed to load topology cache: %s", exc)
            try: self.db.rollback()
            except Exception: pass
            return set()

    def on_message(self, client, userdata, msg):
        """Update live_status and historical_data from ESP32 MQTT telemetry."""
        # Operator weather control is thermal-only and `weather` is not a DB node,
        # so route it before any DB / node_id validation (also works in no-DB mode).
        if msg.topic == "winter-river/weather/control":
            self._handle_weather_control(msg)
            return

        parts = msg.topic.split("/")
        if (
            len(parts) == 3
            and parts[0] == "winter-river"
            and parts[2] == "status"
            and parts[1] in VIRTUAL_STATUS_NODE_IDS
        ):
            return

        # No DB → no node_id validation possible → drop the message.
        if self.db is None:
            return
        try:
            node_id = parts[1]

            # Reject messages from node_ids not in the topology cache. On miss,
            # refresh the cache once (covers re-seeding mid-session) before
            # rejecting — avoids needing a broker restart when init_db.sql runs.
            if node_id not in self._known_nodes:
                self._known_nodes = self._load_known_nodes()
                if node_id not in self._known_nodes:
                    log.warning(
                        "Ignoring MQTT message from unknown node_id %r (topic: %s). "
                        "Is legacy firmware flashed? Run init_db.sql to add new nodes.",
                        node_id, msg.topic,
                    )
                    return

            try:
                payload = json.loads(msg.payload)
            except (json.JSONDecodeError, UnicodeDecodeError):
                payload = {"status": "ONLINE"}

            is_present = payload.get("status") != "OFFLINE"

            # Extract reported state so the broker can react (e.g. OUTAGE → start gen)
            status_from_telemetry = (
                payload.get("state") or payload.get("status") or None
            )

            # Snapshot live fan-bank count from cooling node telemetry so the
            # thermal model gets a measured fan_count instead of a config constant.
            if node_id in self._cooling_fans:
                fr = payload.get("fans_running")
                if isinstance(fr, (int, float)):
                    self._cooling_fans[node_id] = max(
                        0, min(int(fr), self._thermal_cfg.fans_per_module)
                    )
                if not is_present:
                    self._cooling_fans[node_id] = 0

            with self.db.cursor() as cur:
                if status_from_telemetry:
                    cur.execute(
                        "UPDATE live_status SET is_present=%s, status_msg=%s, "
                        "last_update=NOW() WHERE node_id=%s",
                        (is_present, status_from_telemetry, node_id),
                    )
                else:
                    cur.execute(
                        "UPDATE live_status SET is_present=%s, last_update=NOW() "
                        "WHERE node_id=%s",
                        (is_present, node_id),
                    )
                cur.execute(
                    "INSERT INTO historical_data (node_id, metrics) VALUES (%s, %s)",
                    (node_id, json.dumps(payload)),
                )
                self.db.commit()

        except Exception as exc:
            log.error("on_message error: %s", exc)
            try:
                self.db.rollback()
            except Exception:
                pass

    def _handle_weather_control(self, msg):
        """Apply an operator weather command from winter-river/weather/control.

        Space-delimited tokens are processed in order (compound commands):
          PRESET:<1-6>    select a preset, clearing any custom overrides
          RESET           return to the default preset (startup state)
          OUTDOOR_F:<f>   override outdoor dry-bulb temperature (°F)
          RH_PCT:<f>      override relative humidity (clamped 0..100)

        Semantics:
          * Retained messages are ignored so every broker restart truly begins at
            the default preset — runtime commands must be published non-retained.
          * Parsing is atomic: any invalid token logs a warning and leaves the
            current weather (and weather/status) unchanged — nothing is published.
          * A fresh dict is assigned to self._weather (never mutated in place) so
            the simulation-tick thread always reads a complete weather object.
          * On success the new weather/status is republished immediately; the next
            tick recomputes thermal/facility values from it.
        """
        # Startup must always begin at the default preset, so a retained command
        # left on the topic must not re-apply on the next broker boot.
        if getattr(msg, "retain", False):
            log.warning("Ignoring retained weather/control message "
                        "(runtime commands must be non-retained)")
            return

        try:
            text = msg.payload.decode("utf-8").strip()
        except (UnicodeDecodeError, AttributeError):
            log.warning("Ignoring weather/control message with undecodable payload")
            return

        if not text:
            return

        # Build into a local dict; only commit to self._weather if every token
        # parses, so a bad token in a compound command changes nothing.
        w = dict(self._weather)
        custom = bool(w.get("custom", False))

        for token in text.split():
            if token.upper() == "RESET":
                w = resolve_weather({"preset": DEFAULT_WEATHER_PRESET})
                custom = False
                continue

            if ":" not in token:
                log.warning("Ignoring malformed weather token %r", token)
                return

            key, _, value = token.partition(":")
            key = key.upper()
            try:
                if key == "PRESET":
                    preset = int(value)
                    if preset not in WEATHER_PRESETS:
                        raise ValueError(f"unknown preset {preset}")
                    w = resolve_weather({"preset": preset})
                    custom = False
                elif key == "OUTDOOR_F":
                    w["outdoor_f"] = float(value)
                    custom = True
                elif key == "RH_PCT":
                    w["rh_pct"] = max(0.0, min(float(value), 100.0))
                    custom = True
                else:
                    log.warning("Ignoring unknown weather token %r", token)
                    return
            except ValueError as exc:
                log.warning("Ignoring invalid weather token %r (%s)", token, exc)
                return

        if custom:
            w["custom"] = True
        else:
            w.pop("custom", None)

        self._weather = w
        log.info(
            "Weather set via MQTT: %s (%.1f F / %.0f%% RH)%s",
            w.get("name"), w["outdoor_f"], w["rh_pct"],
            " [custom]" if custom else "",
        )
        self._publish_weather_status()

    # ── Topological sort ──────────────────────────────────────────────────────

    def _topo_sort(self, nodes):
        """Kahn's BFS topological sort.
        Handles dual parents (secondary_parent_id) so every parent is fully
        computed before any child.  Returns node IDs in propagation order.
        """
        in_degree = defaultdict(int)
        children  = defaultdict(list)

        for nid, node in nodes.items():
            for pk in ("parent_id", "secondary_parent_id"):
                pid = node.get(pk)
                if pid and pid in nodes:
                    in_degree[nid] += 1
                    children[pid].append(nid)

        # UTILITY and GENERATOR are roots; process UTILITY first so
        # generator startup logic can read utility v_out immediately.
        def _priority(nid):
            t = nodes[nid]["node_type"]
            return 0 if t == "UTILITY" else (1 if t == "GENERATOR" else 2)

        queue = sorted(
            [nid for nid in nodes if in_degree[nid] == 0],
            key=_priority,
        )
        order = []
        while queue:
            nid = queue.pop(0)
            order.append(nid)
            for child in sorted(children[nid]):
                in_degree[child] -= 1
                if in_degree[child] == 0:
                    queue.append(child)

        if len(order) != len(nodes):
            log.warning("Topo sort incomplete (cycle?): %d of %d nodes ordered",
                        len(order), len(nodes))
        return order

    # ── Per-node propagation logic ────────────────────────────────────────────

    def _compute_node(self, node, nodes):
        """Return (v_out, status_msg) for this node given parent states.
        Also mutates node dict for stateful fields (battery_level, gen_timer).
        """
        ntype = node["node_type"]

        parent     = nodes.get(node.get("parent_id"))
        parent_v   = parent["v_out"] if parent else 0.0

        sec_parent   = nodes.get(node.get("secondary_parent_id"))
        sec_parent_v = sec_parent["v_out"] if sec_parent else 0.0

        # Disconnected ESP32 → everything zero
        if not node["is_present"]:
            return 0.0, "OFFLINE"

        # ── UTILITY ──────────────────────────────────────────────────────────
        if ntype == "UTILITY":
            # Firmware owns the state (GRID_OK / SAG / SWELL / OUTAGE / FAULT).
            # Broker reflects it; v_out is nonzero for any energised state.
            status = node["status_msg"]
            v_out  = 0.0 if status in ("OUTAGE", "FAULT", "OFFLINE") else 230000.0
            return v_out, status

        # ── HV_MV_TRANSFORMER ─────────────────────────────────────────────────
        # 230 kV → 34.5 kV step-down, fed directly from utility.
        elif ntype == "HV_MV_TRANSFORMER":
            if parent_v > 0:
                status = node["status_msg"]
                return 34500.0, "NORMAL" if status not in ("WARNING", "FAULT") else status
            return 0.0, "FAULT" if node["status_msg"] == "FAULT" else "NO_INPUT"

        # ── MV_SWITCHGEAR ─────────────────────────────────────────────────────
        # MV switchgear on the 34.5 kV bus, downstream of the HV/MV transformer.
        # Its output feeds the MV/LV transformer.
        elif ntype == "MV_SWITCHGEAR":
            status = node["status_msg"]
            # Operator-open and protective trips are sticky — they hold the
            # breaker open until an explicit CLOSE / STATUS:CLOSED clears them.
            if status in ("OPEN", "TRIPPED", "FAULT"):
                return 0.0, status
            if parent_v > 0:
                return 34500.0, "CLOSED"
            # Unfed is reported as NO_INPUT (NOT sticky) so the breaker re-closes
            # automatically once the upstream chain is re-energised. (A sticky
            # "OPEN" here latched the bus dead and blocked utility recovery.)
            return 0.0, "NO_INPUT"

        # ── MV_LV_TRANSFORMER ─────────────────────────────────────────────────
        # 34.5 kV → 480 V step-down, fed from mv_switchgear (MV bus).
        elif ntype == "MV_LV_TRANSFORMER":
            if parent_v > 0:
                status = node["status_msg"]
                return 480.0, "NORMAL" if status not in ("WARNING", "FAULT") else status
            return 0.0, "FAULT" if node["status_msg"] == "FAULT" else "NO_INPUT"

        # ── LV_SWITCHGEAR (utility↔generator transfer point) ──────────────────
        # 480 V LV bus, downstream of the MV/LV transformer. Absorbs the former
        # ATS role: parent = MV/LV transformer (utility path, preferred),
        # secondary_parent = generator (backup). Its 480 V output energises this
        # side's ups + cooling in parallel.
        elif ntype == "LV_SWITCHGEAR":
            status = node["status_msg"]
            # Operator-open and protective trips are sticky — they drop the LV bus
            # (both sources) until an explicit CLOSE / STATUS:CLOSED clears them.
            if status in ("OPEN", "TRIPPED", "FAULT"):
                return 0.0, status
            if parent_v > 0:
                return 480.0, "CLOSED"        # closed on the utility-derived path
            if sec_parent_v > 0:
                return 480.0, "GENERATOR"     # transferred to the standby generator
            # Both sources dead: NO_INPUT (NOT sticky) so the bus re-energises
            # automatically when either the utility path or the generator returns.
            return 0.0, "NO_INPUT"

        # ── GENERATOR ─────────────────────────────────────────────────────────
        elif ntype == "GENERATOR":
            # Check if utility on this side is energised (using is_present +
            # status_msg set from MQTT telemetry in on_message).
            side    = (node.get("side") or "a").lower()
            utility = nodes.get(f"utility_{side}")
            util_ok = (
                utility is not None
                and utility["is_present"]
                and utility["status_msg"] not in ("OUTAGE", "FAULT", "OFFLINE")
            )

            if util_ok:
                # Utility is live — generator stays on standby; reset startup timer
                node["gen_timer"] = GEN_STARTUP_TICKS
                return 0.0, "STANDBY"

            # Utility failed — run startup sequence
            if node["gen_timer"] > 0:
                node["gen_timer"] -= 1
                return 0.0, "STARTING"

            return 480.0, "RUNNING"

        # ── UPS ───────────────────────────────────────────────────────────────
        # parent = lv_switchgear_a / lv_switchgear_b (the LV transfer point)
        elif ntype == "UPS":
            if parent_v > 0:
                node["battery_level"] = min(100, node["battery_level"] + 1)
                status = "NORMAL" if node["battery_level"] >= 100 else "CHARGING"
                return 480.0, status
            if node["battery_level"] > 0:
                node["battery_level"] -= 1
                return 480.0, "ON_BATTERY"
            return 0.0, "FAULT"

        # ── COOLING ───────────────────────────────────────────────────────────
        # parent = lv_switchgear_a / lv_switchgear_b (parallel to UPS — mech load
        # off the LV bus, so it rides the generator transfer too)
        elif ntype == "COOLING":
            return (480.0, "NORMAL") if parent_v > 0 else (0.0, "OFF")

        # ── SERVER_RACK ───────────────────────────────────────────────────────
        # Single-fed from this side's UPS (480 V AC → 48 V DC inside the rack).
        # No rack-level 2N — side-level (block) redundancy only. The rack reports
        # DEGRADED only while its UPS is actually islanding on battery (ON_BATTERY).
        # Once the side is back on utility/generator power the UPS reads CHARGING and
        # the racks are fed clean power again, so they return to NORMAL — the battery
        # recharging in the background does not degrade the IT load.
        elif ntype == "SERVER_RACK":
            if parent_v > 0:
                ups_status = parent["status_msg"] if parent else "NORMAL"
                if ups_status == "ON_BATTERY":
                    return 48.0, "DEGRADED"
                return 48.0, "NORMAL"
            return 0.0, "FAULT"

        log.warning("Unknown node_type %r for %s", ntype, node["node_id"])
        return 0.0, "UNKNOWN"

    # ── Control command builder ───────────────────────────────────────────────

    def _control_cmd(self, node, v_out, status):
        """Build the control string sent to an ESP32 node."""
        ntype = node["node_type"]

        if ntype == "UTILITY":
            return f"VOLT:{v_out} STATUS:{status}"

        elif ntype == "MV_SWITCHGEAR":
            return f"CLOSE STATUS:{status}" if v_out > 0 else f"OPEN STATUS:{status}"

        elif ntype == "HV_MV_TRANSFORMER":
            return f"STATUS:{status}"

        elif ntype == "LV_SWITCHGEAR":
            return f"CLOSE STATUS:{status}" if v_out > 0 else f"OPEN STATUS:{status}"

        elif ntype == "MV_LV_TRANSFORMER":
            return f"STATUS:{status}"

        elif ntype == "GENERATOR":
            rpm = 1800 if status == "RUNNING" else (600 if status == "STARTING" else 0)
            return f"RPM:{rpm} STATUS:{status}"

        elif ntype == "UPS":
            # Send the UPS *input* voltage (what the LV switchgear delivers), not its output.
            # While islanding on battery (ON_BATTERY / FAULT) the input is dead, so
            # the node's OLED correctly shows Vin:0 during an outage. v_out remains the
            # UPS output used for downstream propagation above; only this firmware-facing
            # INPUT token is derived from the computed status.
            ups_input = 480.0 if status in ("NORMAL", "CHARGING") else 0.0
            return f"INPUT:{ups_input:.1f} BATT:{node['battery_level']} STATUS:{status}"

        elif ntype == "COOLING":
            t = self._latest_thermal
            if v_out > 0 and t is not None:
                cool_status = (
                    "DEGRADED" if t["mode"] in ("OVERHEATING", "UNDERPRESSURED")
                    else status
                )
                return (
                    f"INPUT:{v_out:.1f} TEMP:{t['cold_aisle_f']:.1f} "
                    f"SPEED:{t['flow_pct_max']:.0f} STATUS:{cool_status}"
                )
            return f"INPUT:{v_out:.1f} STATUS:{status}"

        elif ntype == "SERVER_RACK":
            # Single-fed from this side's UPS — no PATH_A / PATH_B at the rack.
            input_v = 480.0 if v_out > 0 else 0.0
            base = f"INPUT:{input_v:.1f} STATUS:{status}"
            t = self._latest_thermal
            if t is not None and math.isfinite(t["hot_aisle_f"]):
                base += f" TEMP:{t['hot_aisle_f']:.1f}"
            return base

        return f"STATUS:{status}"

    # ── Main simulation tick ──────────────────────────────────────────────────

    def _mark_stale_nodes(self):
        """Flip is_present=False for nodes whose last_update is older than
        STALE_NODE_THRESHOLD_SEC. LWT handles clean disconnects; this catches
        silent hangs (TCP keepalive elapses much slower than the 5 s telemetry
        interval). Returning telemetry re-flips is_present via on_message."""
        try:
            with self.db.cursor() as cur:
                cur.execute(
                    "UPDATE live_status SET is_present=FALSE, status_msg='OFFLINE' "
                    "WHERE is_present=TRUE AND last_update < NOW() - %s",
                    (timedelta(seconds=STALE_NODE_THRESHOLD_SEC),),
                )
                stale = cur.rowcount
                self.db.commit()
                if stale:
                    log.info("Watchdog: marked %d node(s) stale", stale)
        except Exception as exc:
            log.warning("Stale-node sweep failed: %s", exc)
            try: self.db.rollback()
            except Exception: pass

    def run_simulation_tick(self):
        """Fetch all node states, propagate voltages, run thermal, push commands."""
        if self.db is None:
            return   # no-DB mode: skip the tick entirely
        try:
            self._mark_stale_nodes()
            with self.db.cursor() as cur:
                cur.execute(
                    """
                    SELECT n.node_id, n.node_type, n.side,
                           n.parent_id, n.secondary_parent_id,
                           n.rated_voltage, n.v_ratio,
                           l.is_present, l.v_in, l.v_out, l.status_msg,
                           l.battery_level, l.gen_timer
                    FROM nodes n JOIN live_status l ON n.node_id = l.node_id
                    """
                )
                nodes = {row["node_id"]: dict(row) for row in cur.fetchall()}

            order = self._topo_sort(nodes)

            # Pass 1: power propagation (mutates each node dict in place).
            for nid in order:
                node = nodes[nid]
                v_out, status = self._compute_node(node, nodes)
                node["v_out"]      = v_out
                node["status_msg"] = status

            # Compute thermal from updated node state — needed before publishing
            # cooling/server_rack control commands so they can carry TEMP/SPEED.
            self._latest_thermal = self._compute_tick_thermal(nodes)

            # Pass 2: publish control commands.
            # QoS 0: control is idempotent and re-sent every tick (≤1 s), so an
            # occasional drop self-heals on the next tick. QoS 1 here meant
            # 24 nodes × 1 Hz of inflight PUBACK traffic; a node servicing MQTT
            # slowly couldn't keep up, the backlog wedged its socket, and
            # Mosquitto dropped it ("MQTT FAILED"). QoS 0 removes that pressure.
            for nid in order:
                node = nodes[nid]
                # Utility is an exogenous input (firmware/manual-owned), not a broker
                # output. Echoing a command back at it would overwrite a manually
                # injected STATUS:OUTAGE with the last telemetry-reported state on the
                # next 1 Hz tick — so the outage never sticks or cascades. The broker
                # still reads utility state from telemetry in on_message and propagates
                # it downstream; recovery is a manual STATUS:GRID_OK on utility/control.
                if node["node_type"] == "UTILITY":
                    continue
                cmd  = self._control_cmd(node, node["v_out"], node["status_msg"])
                self.mqtt_client.publish(f"winter-river/{nid}/control", cmd, qos=0)
                log.debug("→ %s/control: %s", nid, cmd)

            # Publish derived facility + weather state for Telegraf / Grafana.
            self._publish_facility_status(self._latest_thermal)
            self._publish_weather_status()

            with self.db.cursor() as cur:
                for nid, node in nodes.items():
                    cur.execute(
                        """
                        UPDATE live_status
                        SET v_out=%s, status_msg=%s, battery_level=%s, gen_timer=%s
                        WHERE node_id=%s
                        """,
                        (
                            node["v_out"],
                            node["status_msg"],
                            node["battery_level"],
                            node["gen_timer"],
                            nid,
                        ),
                    )
            self.db.commit()

            self._persist_facility_metrics(self._latest_thermal)

            if self._influx_write_api:
                self._write_influx(nodes)
                self._write_influx_facility(self._latest_thermal)

        except Exception as exc:
            log.error("Simulation tick error: %s", exc)
            try:
                self.db.rollback()
            except Exception:
                pass

    # ── Thermal coupling ──────────────────────────────────────────────────────

    def _compute_tick_thermal(self, nodes):
        """Run the thermal model using live fans_running counts from cooling_a/_b.
        Each cooling node simulates 55 fans; A + B → 110 nominal. A side that
        is power-offline contributes 0 fans regardless of its last reported value.
        """
        cool_a = nodes.get("cooling_a")
        cool_b = nodes.get("cooling_b")
        a_on = bool(cool_a and cool_a.get("v_out", 0) > 0)
        b_on = bool(cool_b and cool_b.get("v_out", 0) > 0)
        cooling_online = a_on or b_on

        fans_a = self._cooling_fans.get("cooling_a", 0) if a_on else 0
        fans_b = self._cooling_fans.get("cooling_b", 0) if b_on else 0
        fan_override = max(0, fans_a + fans_b + self._thermal_cfg.fan_diff)

        return compute_thermal(
            outdoor_f=self._weather["outdoor_f"],
            rh_pct=self._weather["rh_pct"],
            cfg=self._thermal_cfg,
            fan_count_override=fan_override,
            cooling_online=cooling_online,
        )

    def _publish_facility_status(self, t):
        if not t:
            return
        per_side_nominal = self._thermal_cfg.fans_per_module
        fans_nominal     = per_side_nominal * 2
        payload = {
            "ts":              time.strftime("%H:%M:%S"),
            "node":            "facility",
            "mode":            t["mode"],
            "pue":             round(t["pue"], 3),
            "p_consumption_mw": round(t["p_consumption_w"] / 1e6, 4),
            "p_data_mw":       round(t["p_data_w"]        / 1e6, 4),
            "p_fan_kw":        round(t["p_fan_w"]         / 1e3, 2),
            "p_loss_kw":       round(t["p_loss_w"]        / 1e3, 2),
            "cold_aisle_f":    round(t["cold_aisle_f"], 2),
            "hot_aisle_f":     round(t["hot_aisle_f"], 2) if math.isfinite(t["hot_aisle_f"]) else None,
            "q_cfm":           int(t["q_cfm"]),
            "fan_pct_max":     round(t["fan_pct_max"], 1),
            "flow_pct_max":    round(t["flow_pct_max"], 1),
            "boost_applied":   bool(t.get("boost_applied", False)),
            "fan_count":       t["fan_count"],
            "fans_running_a":  self._cooling_fans.get("cooling_a", 0),
            "fans_running_b":  self._cooling_fans.get("cooling_b", 0),
            "fans_nominal":    fans_nominal,
            "rack_dp_pa":      round(t["rack_dp_pa"], 2),
            "fan_dp_pa":       round(t["fan_dp_pa"], 2),
            "racks_total":     t["racks_total"],
        }
        self.mqtt_client.publish(
            "winter-river/facility/status",
            json.dumps(payload), qos=1, retain=True,
        )

    def _publish_weather_status(self):
        w = self._weather
        payload = {
            "ts":        time.strftime("%H:%M:%S"),
            "node":      "weather",
            "name":      w.get("name", ""),
            "preset":    w.get("preset", 0),
            "outdoor_f": w["outdoor_f"],
            "rh_pct":    w["rh_pct"],
        }
        # Only present after OUTDOOR_F / RH_PCT overrode a selected preset.
        if w.get("custom"):
            payload["custom"] = True
        self.mqtt_client.publish(
            "winter-river/weather/status",
            json.dumps(payload), qos=1, retain=True,
        )

    def _persist_facility_metrics(self, t):
        if not t or self._facility_metrics_disabled:
            return
        try:
            with self.db.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO facility_metrics (
                        mode, outdoor_f, rh_pct, cold_aisle_f, hot_aisle_f,
                        pue, p_data_w, p_fan_w, p_loss_w, p_consumption_w,
                        q_cfm, fan_pct_max, flow_pct_max,
                        rack_dp_pa, fan_dp_pa, fan_count
                    ) VALUES (
                        %s, %s, %s, %s, %s,
                        %s, %s, %s, %s, %s,
                        %s, %s, %s,
                        %s, %s, %s
                    )
                    """,
                    (
                        t["mode"], t["outdoor_f"], t["rh_pct"],
                        t["cold_aisle_f"],
                        t["hot_aisle_f"] if math.isfinite(t["hot_aisle_f"]) else None,
                        t["pue"], t["p_data_w"], t["p_fan_w"],
                        t["p_loss_w"], t["p_consumption_w"],
                        t["q_cfm"], t["fan_pct_max"], t["flow_pct_max"],
                        t["rack_dp_pa"], t["fan_dp_pa"], t["fan_count"],
                    ),
                )
                self.db.commit()
        except psycopg2.errors.UndefinedTable:
            log.warning(
                "facility_metrics table missing — re-run scripts/init_db.sql to enable "
                "thermal history. Disabling facility_metrics writes for this session."
            )
            self.db.rollback()
            self._facility_metrics_disabled = True
        except Exception as exc:
            log.warning("facility_metrics write failed: %s", exc)
            try: self.db.rollback()
            except Exception: pass

    # ── InfluxDB writer ───────────────────────────────────────────────────────

    def _write_influx(self, nodes):
        """Write one point per node to InfluxDB with computed state."""
        points = []
        for nid, node in nodes.items():
            p = (
                Point("node_state")
                .tag("node_id",   nid)
                .tag("node_type", node["node_type"])
                .tag("side",      node.get("side") or "shared")
                .field("v_out",        float(node["v_out"]))
                .field("is_present",   1 if node["is_present"] else 0)
                .field("battery_level", int(node["battery_level"]))
                .field("gen_timer",     int(node["gen_timer"]))
                .field("status_msg",   str(node["status_msg"]))
            )
            points.append(p)
        try:
            self._influx_write_api.write(
                bucket=self._influx_bucket, record=points
            )
        except Exception as exc:
            log.warning("InfluxDB write error: %s", exc)

    def _write_influx_facility(self, t):
        """Write computed facility metrics (PUE, airflow, pressures) to InfluxDB."""
        if not t:
            return
        p = (
            Point("facility_metrics")
            .tag("mode", t["mode"])
            .field("pue",             float(t["pue"]))
            .field("p_data_w",        float(t["p_data_w"]))
            .field("p_fan_w",         float(t["p_fan_w"]))
            .field("p_loss_w",        float(t["p_loss_w"]))
            .field("p_consumption_w", float(t["p_consumption_w"]))
            .field("cold_aisle_f",    float(t["cold_aisle_f"]))
            .field("q_cfm",           float(t["q_cfm"]))
            .field("fan_pct_max",     float(t["fan_pct_max"]))
            .field("flow_pct_max",    float(t["flow_pct_max"]))
            .field("rack_dp_pa",      float(t["rack_dp_pa"]))
            .field("fan_dp_pa",       float(t["fan_dp_pa"]))
            .field("outdoor_f",       float(t["outdoor_f"]))
            .field("rh_pct",          float(t["rh_pct"]))
            .field("fan_count",       int(t["fan_count"]))
            .field("boost_applied",   1 if t.get("boost_applied") else 0)
        )
        if math.isfinite(t["hot_aisle_f"]):
            p = p.field("hot_aisle_f", float(t["hot_aisle_f"]))
        try:
            self._influx_write_api.write(bucket=self._influx_bucket, record=[p])
        except Exception as exc:
            log.warning("InfluxDB facility write error: %s", exc)


# ── ENTRY POINT ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    engine = WinterRiverEngine()
    log.info("Running at %.1f s tick rate — Ctrl-C to stop", TICK_RATE)
    while True:
        engine.run_simulation_tick()
        time.sleep(TICK_RATE)
