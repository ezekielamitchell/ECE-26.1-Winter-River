"""Unit tests for broker/main.py::WinterRiverEngine.

Covers the propagation state machine (`_topo_sort`, `_compute_node`,
`_control_cmd`) and the MQTT ingestion path (`on_message`). All tests run
without real MQTT / Postgres — `WinterRiverEngine.__new__` skips `__init__`,
and `self.db` is replaced with a mock when needed.
"""

import json
import math
from unittest.mock import MagicMock

import pytest

import main as broker_main
from main import GEN_STARTUP_TICKS, WinterRiverEngine
from thermal import ThermalConfig


# ── helpers ───────────────────────────────────────────────────────────────────

def _node(node_id, node_type, **kw):
    """Build a node dict shaped like a row from the broker's SELECT join."""
    return {
        "node_id":             node_id,
        "node_type":           node_type,
        "side":                kw.get("side"),
        "parent_id":           kw.get("parent_id"),
        "secondary_parent_id": kw.get("secondary_parent_id"),
        "is_present":          kw.get("is_present", True),
        "v_in":                kw.get("v_in", 0.0),
        "v_out":               kw.get("v_out", 0.0),
        "status_msg":          kw.get("status_msg", "NORMAL"),
        "battery_level":       kw.get("battery_level", 100),
        "gen_timer":           kw.get("gen_timer", GEN_STARTUP_TICKS),
        "rated_voltage":       kw.get("rated_voltage", 480.0),
        "v_ratio":             kw.get("v_ratio", 1.0),
    }


@pytest.fixture
def engine():
    """Bare engine with no DB / MQTT — just the attrs the pure methods touch."""
    eng = WinterRiverEngine.__new__(WinterRiverEngine)
    eng._latest_thermal = None
    eng._thermal_cfg = ThermalConfig()
    eng._cooling_fans = {"cooling_a": 55, "cooling_b": 55}
    eng.db = MagicMock()
    return eng


# ── _topo_sort ────────────────────────────────────────────────────────────────

class TestTopoSort:
    def test_linear_chain(self, engine):
        nodes = {
            "a": _node("a", "UTILITY"),
            "b": _node("b", "HV_MV_TRANSFORMER", parent_id="a"),
            "c": _node("c", "MV_SWITCHGEAR",     parent_id="b"),
        }
        order = engine._topo_sort(nodes)
        assert order == ["a", "b", "c"]

    def test_dual_parent_rectifier_after_both_ups(self, engine):
        nodes = {
            "ups_a":     _node("ups_a",     "UPS"),
            "ups_b":     _node("ups_b",     "UPS"),
            "rectifier": _node("rectifier", "RECTIFIER",
                               parent_id="ups_a",
                               secondary_parent_id="ups_b"),
        }
        order = engine._topo_sort(nodes)
        assert order.index("rectifier") > order.index("ups_a")
        assert order.index("rectifier") > order.index("ups_b")

    def test_utility_scheduled_before_generator(self, engine):
        """Both are roots; broker prioritises UTILITY so generator can read it."""
        nodes = {
            "generator_a": _node("generator_a", "GENERATOR", side="a"),
            "utility_a":   _node("utility_a",   "UTILITY"),
        }
        order = engine._topo_sort(nodes)
        assert order.index("utility_a") < order.index("generator_a")

    def test_cycle_returns_partial_order_without_raising(self, engine, caplog):
        """A → B → A. Pure cycle — neither has in_degree 0, so order is empty.
        The broker must log a warning instead of crashing the tick.
        """
        nodes = {
            "a": _node("a", "LV_DIST", parent_id="b"),
            "b": _node("b", "LV_DIST", parent_id="a"),
        }
        order = engine._topo_sort(nodes)
        assert order == []  # nothing was schedulable
        assert any("cycle" in r.message.lower() for r in caplog.records)

    def test_parent_outside_node_set_is_ignored(self, engine):
        """parent_id pointing at a node not in the dict shouldn't make us hang."""
        nodes = {
            "child": _node("child", "LV_DIST", parent_id="ghost"),
        }
        order = engine._topo_sort(nodes)
        assert order == ["child"]


# ── _compute_node ─────────────────────────────────────────────────────────────

class TestComputeNode:
    # UTILITY
    def test_utility_grid_ok(self, engine):
        n = _node("utility_a", "UTILITY", status_msg="GRID_OK")
        v, s = engine._compute_node(n, {n["node_id"]: n})
        assert v == 230000.0 and s == "GRID_OK"

    @pytest.mark.parametrize("status", ["OUTAGE", "FAULT", "OFFLINE"])
    def test_utility_dead_states_zero_out(self, engine, status):
        n = _node("utility_a", "UTILITY", status_msg=status)
        v, s = engine._compute_node(n, {n["node_id"]: n})
        assert v == 0.0 and s == status

    def test_node_not_present_is_offline(self, engine):
        n = _node("any", "LV_DIST", is_present=False, status_msg="NORMAL")
        v, s = engine._compute_node(n, {n["node_id"]: n})
        assert v == 0.0 and s == "OFFLINE"

    # HV_MV_TRANSFORMER
    def test_hv_mv_transformer_steps_down(self, engine):
        parent = _node("u", "UTILITY", v_out=230000.0)
        n = _node("t", "HV_MV_TRANSFORMER", parent_id="u")
        v, s = engine._compute_node(n, {"u": parent, "t": n})
        assert v == 34500.0 and s == "NORMAL"

    def test_hv_mv_transformer_propagates_fault(self, engine):
        parent = _node("u", "UTILITY", v_out=230000.0)
        n = _node("t", "HV_MV_TRANSFORMER", parent_id="u", status_msg="FAULT")
        v, s = engine._compute_node(n, {"u": parent, "t": n})
        assert v == 34500.0 and s == "FAULT"

    # MV_SWITCHGEAR
    def test_mv_switchgear_closes_when_fed(self, engine):
        parent = _node("p", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="p")
        v, s = engine._compute_node(n, {"p": parent, "sw": n})
        assert v == 34500.0 and s == "CLOSED"

    @pytest.mark.parametrize("sticky", ["TRIPPED", "FAULT"])
    def test_mv_switchgear_sticky_faults_survive_re_energise(self, engine, sticky):
        parent = _node("p", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="p", status_msg=sticky)
        v, s = engine._compute_node(n, {"p": parent, "sw": n})
        assert v == 0.0 and s == sticky

    # GENERATOR — startup timer
    def test_generator_standby_when_utility_alive(self, engine):
        utility = _node("utility_a", "UTILITY", status_msg="GRID_OK")
        gen = _node("generator_a", "GENERATOR", side="a", gen_timer=3)
        v, s = engine._compute_node(gen, {"utility_a": utility, "generator_a": gen})
        assert (v, s) == (0.0, "STANDBY")
        assert gen["gen_timer"] == GEN_STARTUP_TICKS  # reset

    def test_generator_starting_then_running(self, engine):
        utility = _node("utility_a", "UTILITY", is_present=True, status_msg="OUTAGE")
        gen = _node("generator_a", "GENERATOR", side="a", gen_timer=2)
        nodes = {"utility_a": utility, "generator_a": gen}

        v, s = engine._compute_node(gen, nodes)
        assert (v, s) == (0.0, "STARTING") and gen["gen_timer"] == 1
        v, s = engine._compute_node(gen, nodes)
        assert (v, s) == (0.0, "STARTING") and gen["gen_timer"] == 0
        v, s = engine._compute_node(gen, nodes)
        assert (v, s) == (480.0, "RUNNING")

    def test_generator_treats_missing_utility_as_dead(self, engine):
        gen = _node("generator_z", "GENERATOR", side="z", gen_timer=0)
        v, s = engine._compute_node(gen, {"generator_z": gen})
        assert (v, s) == (480.0, "RUNNING")

    # ATS — prefers primary over secondary
    def test_ats_prefers_utility_path(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=480.0)
        sec  = _node("gen",  "GENERATOR",         v_out=480.0)
        ats  = _node("ats",  "ATS", parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(ats, {"xfmr": prim, "gen": sec, "ats": ats})
        assert (v, s) == (480.0, "UTILITY")

    def test_ats_falls_back_to_generator(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=0.0)
        sec  = _node("gen",  "GENERATOR",         v_out=480.0)
        ats  = _node("ats",  "ATS", parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(ats, {"xfmr": prim, "gen": sec, "ats": ats})
        assert (v, s) == (480.0, "GENERATOR")

    def test_ats_open_when_both_dead(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=0.0)
        sec  = _node("gen",  "GENERATOR",         v_out=0.0)
        ats  = _node("ats",  "ATS", parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(ats, {"xfmr": prim, "gen": sec, "ats": ats})
        assert (v, s) == (0.0, "OPEN")

    # UPS — charge / discharge state machine
    def test_ups_charging_then_normal(self, engine):
        parent = _node("dist", "LV_DIST", v_out=480.0)
        ups = _node("ups", "UPS", parent_id="dist", battery_level=98)
        v, s = engine._compute_node(ups, {"dist": parent, "ups": ups})
        assert (v, s) == (480.0, "CHARGING") and ups["battery_level"] == 99
        v, s = engine._compute_node(ups, {"dist": parent, "ups": ups})
        assert (v, s) == (480.0, "NORMAL") and ups["battery_level"] == 100
        # already at 100 — clamp, don't overflow
        v, s = engine._compute_node(ups, {"dist": parent, "ups": ups})
        assert ups["battery_level"] == 100

    def test_ups_on_battery_then_fault(self, engine):
        parent = _node("dist", "LV_DIST", v_out=0.0)
        ups = _node("ups", "UPS", parent_id="dist", battery_level=1)
        v, s = engine._compute_node(ups, {"dist": parent, "ups": ups})
        assert (v, s) == (480.0, "ON_BATTERY") and ups["battery_level"] == 0
        v, s = engine._compute_node(ups, {"dist": parent, "ups": ups})
        assert (v, s) == (0.0, "FAULT")

    # RECTIFIER — true 2N truth table
    @pytest.mark.parametrize("a_v, b_v, expected", [
        (480.0, 480.0, (48.0, "NORMAL")),
        (480.0,   0.0, (48.0, "DEGRADED")),
        (  0.0, 480.0, (48.0, "DEGRADED")),
        (  0.0,   0.0, ( 0.0, "OFF")),
    ])
    def test_rectifier_2n_truth_table(self, engine, a_v, b_v, expected):
        a = _node("ups_a", "UPS", v_out=a_v)
        b = _node("ups_b", "UPS", v_out=b_v)
        r = _node("rectifier", "RECTIFIER",
                  parent_id="ups_a", secondary_parent_id="ups_b")
        v, s = engine._compute_node(r, {"ups_a": a, "ups_b": b, "rectifier": r})
        assert (v, s) == expected

    # COOLING / LIGHTING
    def test_cooling_off_with_no_input(self, engine):
        parent = _node("dist", "LV_DIST", v_out=0.0)
        n = _node("cool", "COOLING", parent_id="dist")
        assert engine._compute_node(n, {"dist": parent, "cool": n}) == (0.0, "OFF")

    def test_lighting_on_277v_when_fed(self, engine):
        parent = _node("dist", "LV_DIST", v_out=480.0)
        n = _node("light", "LIGHTING", parent_id="dist")
        assert engine._compute_node(n, {"dist": parent, "light": n}) == (277.0, "ON")

    # SERVER_RACK — inherits rectifier DEGRADED
    def test_server_rack_inherits_degraded(self, engine):
        rect = _node("rectifier", "RECTIFIER", v_out=48.0, status_msg="DEGRADED")
        rack = _node("server_rack", "SERVER_RACK", parent_id="rectifier")
        v, s = engine._compute_node(rack, {"rectifier": rect, "server_rack": rack})
        assert (v, s) == (48.0, "DEGRADED")

    def test_server_rack_fault_without_rectifier_feed(self, engine):
        rect = _node("rectifier", "RECTIFIER", v_out=0.0, status_msg="OFF")
        rack = _node("server_rack", "SERVER_RACK", parent_id="rectifier")
        v, s = engine._compute_node(rack, {"rectifier": rect, "server_rack": rack})
        assert (v, s) == (0.0, "FAULT")

    def test_unknown_node_type_returns_unknown(self, engine, caplog):
        n = _node("weird", "WORMHOLE")
        v, s = engine._compute_node(n, {"weird": n})
        assert (v, s) == (0.0, "UNKNOWN")
        assert any("Unknown node_type" in r.message for r in caplog.records)


# ── _control_cmd ──────────────────────────────────────────────────────────────

class TestControlCmd:
    def test_utility_format(self, engine):
        n = _node("u", "UTILITY")
        assert engine._control_cmd(n, 230000.0, "GRID_OK") == \
            "VOLT:230000.0 STATUS:GRID_OK"

    def test_mv_switchgear_close_vs_open(self, engine):
        n = _node("sw", "MV_SWITCHGEAR")
        assert engine._control_cmd(n, 34500.0, "CLOSED") == "CLOSE STATUS:CLOSED"
        assert engine._control_cmd(n, 0.0, "OPEN") == "OPEN STATUS:OPEN"

    def test_generator_rpm_by_status(self, engine):
        n = _node("g", "GENERATOR")
        assert engine._control_cmd(n, 480.0, "RUNNING").startswith("RPM:1800")
        assert engine._control_cmd(n, 0.0, "STARTING").startswith("RPM:600")
        assert engine._control_cmd(n, 0.0, "STANDBY").startswith("RPM:0")

    def test_ups_includes_battery(self, engine):
        n = _node("u", "UPS", battery_level=72)
        out = engine._control_cmd(n, 480.0, "CHARGING")
        assert "BATT:72" in out and "STATUS:CHARGING" in out

    def test_rectifier_reports_both_paths(self, engine):
        n = _node("r", "RECTIFIER")
        n["parent_v_a"] = 480.0
        n["parent_v_b"] = 0.0
        out = engine._control_cmd(n, 48.0, "DEGRADED")
        assert "PATH_A:1" in out and "PATH_B:0" in out and "INPUT_AC:480.0" in out

    def test_cooling_carries_thermal_when_available(self, engine):
        engine._latest_thermal = {
            "mode": "NORMAL",
            "cold_aisle_f": 75.2,
            "flow_pct_max": 42.0,
            "hot_aisle_f": 95.0,
        }
        n = _node("c", "COOLING")
        out = engine._control_cmd(n, 480.0, "NORMAL")
        assert "TEMP:75.2" in out and "SPEED:42" in out

    def test_cooling_degrades_when_thermal_overheating(self, engine):
        engine._latest_thermal = {
            "mode": "OVERHEATING",
            "cold_aisle_f": 110.0,
            "flow_pct_max": 100.0,
            "hot_aisle_f": 130.0,
        }
        n = _node("c", "COOLING")
        out = engine._control_cmd(n, 480.0, "NORMAL")
        assert "STATUS:DEGRADED" in out

    def test_cooling_omits_thermal_when_off(self, engine):
        engine._latest_thermal = {
            "mode": "NORMAL", "cold_aisle_f": 75.0,
            "flow_pct_max": 50.0, "hot_aisle_f": 90.0,
        }
        n = _node("c", "COOLING")
        out = engine._control_cmd(n, 0.0, "OFF")
        assert "TEMP" not in out and "STATUS:OFF" in out

    def test_server_rack_omits_hot_aisle_when_inf(self, engine):
        engine._latest_thermal = {
            "mode": "FAULT", "hot_aisle_f": math.inf,
            "cold_aisle_f": 100.0, "flow_pct_max": 0.0,
        }
        n = _node("rack", "SERVER_RACK")
        n["parent_v_a"] = 0.0
        n["parent_v_b"] = 0.0
        out = engine._control_cmd(n, 0.0, "FAULT")
        assert "TEMP" not in out


# ── on_message — MQTT ingestion ───────────────────────────────────────────────

class _FakeCursor:
    """Minimal cursor that supports the broker's two access patterns:
       1. context-manager (`with db.cursor() as cur:`),
       2. configurable `fetchone()` to simulate node_id lookups.
    """
    def __init__(self, recorder, known_nodes):
        self._rec = recorder
        self._known = known_nodes
        self._last = None

    def __enter__(self):  return self
    def __exit__(self, *a): return False

    def execute(self, sql, params=()):
        self._rec.append((sql, params))
        self._last = (sql, params)

    def fetchone(self):
        sql, params = self._last
        if "SELECT 1 FROM nodes" in sql:
            return (1,) if params[0] in self._known else None
        return None


def _make_msg(topic, payload):
    msg = MagicMock()
    msg.topic = topic
    msg.payload = payload if isinstance(payload, (bytes, bytearray)) \
        else payload.encode()
    return msg


@pytest.fixture
def ingest_engine():
    eng = WinterRiverEngine.__new__(WinterRiverEngine)
    eng._latest_thermal = None
    eng._thermal_cfg = ThermalConfig()
    eng._cooling_fans = {"cooling_a": 55, "cooling_b": 55}
    eng._known_nodes = {"utility_a", "cooling_a", "cooling_b", "ups_a"}
    eng._exec_log = []

    eng.db = MagicMock()
    eng.db.cursor = lambda: _FakeCursor(eng._exec_log, eng._known_nodes)
    return eng


class TestOnMessage:
    def test_unknown_node_id_is_rejected_without_writes(self, ingest_engine):
        msg = _make_msg("winter-river/ghost/status", '{"status":"ONLINE"}')
        ingest_engine.on_message(None, None, msg)
        # Only the SELECT for FK pre-check should have run — no UPDATE/INSERT.
        sqls = [s for s, _ in ingest_engine._exec_log]
        assert len(sqls) == 1 and "SELECT" in sqls[0]
        ingest_engine.db.commit.assert_not_called()

    def test_malformed_json_defaults_to_online(self, ingest_engine):
        msg = _make_msg("winter-river/utility_a/status", b"\xff not-json")
        ingest_engine.on_message(None, None, msg)
        # Found the node, ran UPDATE + INSERT, committed once.
        sqls = [s for s, _ in ingest_engine._exec_log]
        assert any("UPDATE live_status" in s for s in sqls)
        assert any("INSERT INTO historical_data" in s for s in sqls)
        ingest_engine.db.commit.assert_called_once()

    def test_fans_running_clamped_to_module_max(self, ingest_engine):
        payload = json.dumps({"status": "ONLINE", "fans_running": 9999})
        ingest_engine.on_message(
            None, None, _make_msg("winter-river/cooling_a/status", payload)
        )
        assert ingest_engine._cooling_fans["cooling_a"] == \
            ingest_engine._thermal_cfg.fans_per_module

    def test_fans_running_clamped_below_zero(self, ingest_engine):
        payload = json.dumps({"status": "ONLINE", "fans_running": -5})
        ingest_engine.on_message(
            None, None, _make_msg("winter-river/cooling_a/status", payload)
        )
        assert ingest_engine._cooling_fans["cooling_a"] == 0

    def test_offline_cooling_zeroes_fan_count(self, ingest_engine):
        ingest_engine._cooling_fans["cooling_b"] = 55
        payload = json.dumps({"status": "OFFLINE", "fans_running": 40})
        ingest_engine.on_message(
            None, None, _make_msg("winter-river/cooling_b/status", payload)
        )
        assert ingest_engine._cooling_fans["cooling_b"] == 0

    def test_db_error_triggers_rollback(self, ingest_engine):
        # First execute (the FK pre-check) raises — must hit the except / rollback.
        boom = MagicMock()
        boom.__enter__ = MagicMock(side_effect=RuntimeError("db gone"))
        boom.__exit__  = MagicMock(return_value=False)
        ingest_engine.db.cursor = lambda: boom

        ingest_engine.on_message(
            None, None, _make_msg("winter-river/utility_a/status", '{"status":"ONLINE"}')
        )
        ingest_engine.db.rollback.assert_called_once()


# ── module-level smoke ────────────────────────────────────────────────────────

def test_module_loads_with_seeded_config():
    """conftest copies config.sample.toml → config.toml when missing, so just
    re-importing should never raise. Guards against accidental drift between
    sample and the keys main.py reads (mqtt.broker_host, database.dsn, etc.).
    """
    assert hasattr(broker_main, "WinterRiverEngine")
    assert broker_main.MQTT_PORT == 1883
