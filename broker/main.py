"""
Winter River Simulation Engine
Reads ESP32 MQTT telemetry → propagates power hierarchy → publishes control commands.
Topology: 24 nodes (12 Side A + 12 Side B) + 1 shared server_rack (2N).
Tick rate: 1 Hz (configurable in config.toml).
"""

import json
import logging
import os
import time
from collections import defaultdict

import paho.mqtt.client as mqtt
import psycopg2
import toml
from psycopg2.extras import RealDictCursor

try:
    from influxdb_client import InfluxDBClient, Point
    from influxdb_client.client.write_api import SYNCHRONOUS
    HAS_INFLUX = True
except ImportError:
    HAS_INFLUX = False

# ── CONFIGURATION ─────────────────────────────────────────────────────────────

_cfg_path = os.path.join(os.path.dirname(__file__), "config.toml")
_cfg = toml.load(_cfg_path)

MQTT_BROKER = _cfg["mqtt"]["broker_host"]
MQTT_PORT   = _cfg["mqtt"]["broker_port"]
DB_CONFIG   = _cfg["database"]["dsn"]
TICK_RATE   = _cfg.get("simulation", {}).get("tick_rate", 1.0)

# Generator startup delay in simulation ticks (1 tick = 1 s at default tick rate)
GEN_STARTUP_TICKS = 10

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("winter-river")

# ── ENGINE ────────────────────────────────────────────────────────────────────

class WinterRiverEngine:
    def __init__(self):
        self.db = psycopg2.connect(DB_CONFIG, cursor_factory=RealDictCursor)

        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_message = self.on_message
        self.mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
        self.mqtt_client.subscribe("winter-river/+/status", qos=1)
        self.mqtt_client.loop_start()
        log.info("MQTT connected to %s:%d", MQTT_BROKER, MQTT_PORT)

        # Optional InfluxDB direct writes (computed state, 1 Hz)
        self._influx_write_api = None
        self._influx_bucket    = None
        if HAS_INFLUX and "influxdb" in _cfg:
            icfg = _cfg["influxdb"]
            try:
                self._influx_client  = InfluxDBClient(
                    url=icfg["url"], token=icfg["token"], org=icfg["org"]
                )
                self._influx_write_api = self._influx_client.write_api(
                    write_options=SYNCHRONOUS
                )
                self._influx_bucket = icfg["bucket"]
                log.info("InfluxDB connected to %s", icfg["url"])
            except Exception as exc:
                log.warning("InfluxDB init failed (continuing without): %s", exc)

        log.info("Winter River Engine initialised")

    # ── MQTT ingestion ────────────────────────────────────────────────────────

    def on_message(self, client, userdata, msg):
        """Update live_status and historical_data from ESP32 MQTT telemetry."""
        try:
            node_id = msg.topic.split("/")[1]

            # Reject messages from node_ids not in the DB — avoids FK violations
            # and catches accidentally-flashed legacy firmware (util_a, gen_a, etc.)
            with self.db.cursor() as cur:
                cur.execute("SELECT 1 FROM nodes WHERE node_id=%s", (node_id,))
                if cur.fetchone() is None:
                    log.warning(
                        "Ignoring MQTT message from unknown node_id %r (topic: %s). "
                        "Is legacy firmware flashed? Run init_db.sql to add new nodes.",
                        node_id, msg.topic,
                    )
                    return

            try:
                payload = json.loads(msg.payload)
            except (json.JSONDecodeError, UnicodeDecodeError):
                # pdu_a still publishes a plain string
                payload = {"status": "ONLINE"}

            is_present = payload.get("status") != "OFFLINE"

            # Extract reported state so the broker can react (e.g. OUTAGE → start gen)
            status_from_telemetry = (
                payload.get("state") or payload.get("status") or None
            )

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

        # ── MV_SWITCHGEAR ─────────────────────────────────────────────────────
        elif ntype == "MV_SWITCHGEAR":
            status = node["status_msg"]
            if parent_v > 0 and status not in ("OPEN", "TRIPPED", "FAULT"):
                return 34500.0, "CLOSED"
            return 0.0, status if status in ("TRIPPED", "FAULT") else "OPEN"

        # ── MV_LV_TRANSFORMER ─────────────────────────────────────────────────
        elif ntype == "MV_LV_TRANSFORMER":
            if parent_v > 0:
                status = node["status_msg"]
                return 480.0, "NORMAL" if status not in ("WARNING", "FAULT") else status
            return 0.0, "FAULT" if node["status_msg"] == "FAULT" else "NO_INPUT"

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

        # ── ATS (Automatic Transfer Switch) ───────────────────────────────────
        # parent          = transformer path (preferred)
        # secondary_parent = generator path (backup)
        elif ntype == "ATS":
            if parent_v > 0:
                return parent_v, "UTILITY"
            if sec_parent_v > 0:
                return sec_parent_v, "GENERATOR"
            return 0.0, "OPEN"

        # ── LV_DIST ───────────────────────────────────────────────────────────
        elif ntype == "LV_DIST":
            if parent_v > 0:
                status = node["status_msg"]
                return 480.0, "NORMAL" if status not in ("OVERLOAD", "FAULT") else status
            return 0.0, "NO_INPUT"

        # ── UPS ───────────────────────────────────────────────────────────────
        elif ntype == "UPS":
            if parent_v > 0:
                node["battery_level"] = min(100, node["battery_level"] + 1)
                status = "NORMAL" if node["battery_level"] >= 100 else "CHARGING"
                return 480.0, status
            if node["battery_level"] > 0:
                node["battery_level"] -= 1
                return 480.0, "ON_BATTERY"
            return 0.0, "FAULT"

        # ── PDU ───────────────────────────────────────────────────────────────
        elif ntype == "PDU":
            return (480.0, "NORMAL") if parent_v > 0 else (0.0, "OFF")

        # ── RECTIFIER (480 V AC → 48 V DC) ───────────────────────────────────
        elif ntype == "RECTIFIER":
            return (48.0, "NORMAL") if parent_v > 0 else (0.0, "OFF")

        # ── COOLING ───────────────────────────────────────────────────────────
        elif ntype == "COOLING":
            return (480.0, "NORMAL") if parent_v > 0 else (0.0, "OFF")

        # ── LIGHTING ──────────────────────────────────────────────────────────
        elif ntype == "LIGHTING":
            return (277.0, "ON") if parent_v > 0 else (0.0, "OFF")

        # ── MONITORING ────────────────────────────────────────────────────────
        elif ntype == "MONITORING":
            return (120.0, "NORMAL") if parent_v > 0 else (0.0, "OFF")

        # ── SERVER_RACK (2N) ──────────────────────────────────────────────────
        # parent          = rectifier_a
        # secondary_parent = rectifier_b
        elif ntype == "SERVER_RACK":
            a_ok = parent_v > 0
            b_ok = sec_parent_v > 0
            if a_ok and b_ok:
                return 48.0, "NORMAL"
            if a_ok or b_ok:
                return 48.0, "DEGRADED"
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

        elif ntype == "MV_LV_TRANSFORMER":
            return f"STATUS:{status}"

        elif ntype == "GENERATOR":
            rpm = 1800 if status == "RUNNING" else (600 if status == "STARTING" else 0)
            return f"RPM:{rpm} STATUS:{status}"

        elif ntype == "ATS":
            return f"SOURCE:{status} STATUS:{status}"

        elif ntype == "LV_DIST":
            return f"INPUT:{v_out:.1f} STATUS:{status}"

        elif ntype == "UPS":
            return (
                f"INPUT:{v_out:.1f} BATT:{node['battery_level']} STATUS:{status}"
            )

        elif ntype == "PDU":
            return f"INPUT:{v_out:.1f} STATUS:{status}"

        elif ntype == "RECTIFIER":
            ac_in = 480.0 if v_out > 0 else 0.0
            return f"INPUT_AC:{ac_in:.1f} STATUS:{status}"

        elif ntype in ("COOLING", "MONITORING"):
            return f"INPUT:{v_out:.1f} STATUS:{status}"

        elif ntype == "LIGHTING":
            return f"INPUT:{v_out:.1f} STATUS:{status}"

        elif ntype == "SERVER_RACK":
            # Indicate which rectifier paths are live
            path_a = 1 if (node.get("parent_v_a", 0) > 0) else 0
            path_b = 1 if (node.get("parent_v_b", 0) > 0) else 0
            return f"PATH_A:{path_a} PATH_B:{path_b} STATUS:{status}"

        return f"STATUS:{status}"

    # ── Main simulation tick ──────────────────────────────────────────────────

    def run_simulation_tick(self):
        """Fetch all node states, propagate voltages top-down, push commands."""
        try:
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

            for nid in order:
                node       = nodes[nid]
                v_out, status = self._compute_node(node, nodes)

                # Stash for SERVER_RACK path indicator in control cmd
                if node["node_type"] == "SERVER_RACK":
                    parent   = nodes.get(node.get("parent_id"))
                    sec      = nodes.get(node.get("secondary_parent_id"))
                    node["parent_v_a"] = parent["v_out"] if parent else 0.0
                    node["parent_v_b"] = sec["v_out"]    if sec    else 0.0

                node["v_out"]     = v_out
                node["status_msg"] = status

                # Publish control command to ESP32
                cmd = self._control_cmd(node, v_out, status)
                self.mqtt_client.publish(
                    f"winter-river/{nid}/control", cmd, qos=1,
                )
                log.debug("→ %s/control: %s", nid, cmd)

            # Persist updated state for all nodes in one batch
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

            # Write computed state to InfluxDB (optional)
            if self._influx_write_api:
                self._write_influx(nodes)

        except Exception as exc:
            log.error("Simulation tick error: %s", exc)
            try:
                self.db.rollback()
            except Exception:
                pass

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


# ── ENTRY POINT ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    engine = WinterRiverEngine()
    log.info("Running at %.1f s tick rate — Ctrl-C to stop", TICK_RATE)
    while True:
        engine.run_simulation_tick()
        time.sleep(TICK_RATE)
