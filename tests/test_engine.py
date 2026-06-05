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
from thermal import ThermalConfig, resolve_weather


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
            "c": _node("c", "LV_SWITCHGEAR",     parent_id="b"),
        }
        order = engine._topo_sort(nodes)
        assert order == ["a", "b", "c"]

    def test_dual_parent_lv_switchgear_after_both_sources(self, engine):
        """lv_switchgear is the dual-parent transfer node (it absorbed the ATS
        role) — primary = MV/LV transformer path, secondary = generator. Topo
        sort must place it after both sources."""
        nodes = {
            "mv_lv_transformer_a": _node("mv_lv_transformer_a", "MV_LV_TRANSFORMER"),
            "generator_a":         _node("generator_a", "GENERATOR", side="a"),
            "lv_switchgear_a":     _node("lv_switchgear_a", "LV_SWITCHGEAR",
                                         parent_id="mv_lv_transformer_a",
                                         secondary_parent_id="generator_a"),
        }
        order = engine._topo_sort(nodes)
        assert order.index("lv_switchgear_a") > order.index("mv_lv_transformer_a")
        assert order.index("lv_switchgear_a") > order.index("generator_a")

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
            "a": _node("a", "UPS", parent_id="b"),
            "b": _node("b", "UPS", parent_id="a"),
        }
        order = engine._topo_sort(nodes)
        assert order == []  # nothing was schedulable
        assert any("cycle" in r.message.lower() for r in caplog.records)

    def test_parent_outside_node_set_is_ignored(self, engine):
        """parent_id pointing at a node not in the dict shouldn't make us hang."""
        nodes = {
            "child": _node("child", "UPS", parent_id="ghost"),
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
        n = _node("any", "UPS", is_present=False, status_msg="NORMAL")
        v, s = engine._compute_node(n, {n["node_id"]: n})
        assert v == 0.0 and s == "OFFLINE"

    # MV_SWITCHGEAR — switchgear downstream of HV/MV xfmr, on the 34.5 kV MV bus
    def test_mv_switchgear_closes_when_fed(self, engine):
        parent = _node("t", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="t")
        v, s = engine._compute_node(n, {"t": parent, "sw": n})
        assert v == 34500.0 and s == "CLOSED"

    @pytest.mark.parametrize("sticky", ["TRIPPED", "FAULT"])
    def test_mv_switchgear_sticky_faults_survive_re_energise(self, engine, sticky):
        parent = _node("t", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="t", status_msg=sticky)
        v, s = engine._compute_node(n, {"t": parent, "sw": n})
        assert v == 0.0 and s == sticky

    def test_mv_switchgear_no_input_when_unfed(self, engine):
        """Unfed reports the NON-sticky NO_INPUT (not OPEN), so the breaker
        re-closes automatically when the upstream chain re-energises."""
        parent = _node("t", "HV_MV_TRANSFORMER", v_out=0.0, status_msg="NO_INPUT")
        n = _node("sw", "MV_SWITCHGEAR", parent_id="t")
        v, s = engine._compute_node(n, {"t": parent, "sw": n})
        assert v == 0.0 and s == "NO_INPUT"

    def test_mv_switchgear_rerecloses_after_unfed(self, engine):
        """NO_INPUT must clear once parent voltage returns (regression guard for
        the OPEN latch that previously blocked utility recovery)."""
        parent = _node("t", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="t", status_msg="NO_INPUT")
        v, s = engine._compute_node(n, {"t": parent, "sw": n})
        assert v == 34500.0 and s == "CLOSED"

    @pytest.mark.parametrize("sticky", ["OPEN", "TRIPPED", "FAULT"])
    def test_mv_switchgear_operator_open_and_trips_are_sticky(self, engine, sticky):
        """Operator OPEN and protective trips hold the breaker open even when
        fed, until an explicit CLOSE / STATUS:CLOSED clears them."""
        parent = _node("t", "HV_MV_TRANSFORMER", v_out=34500.0)
        n = _node("sw", "MV_SWITCHGEAR", parent_id="t", status_msg=sticky)
        v, s = engine._compute_node(n, {"t": parent, "sw": n})
        assert v == 0.0 and s == sticky

    # HV_MV_TRANSFORMER — now fed directly from utility (no upstream switchgear)
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

    # LV_SWITCHGEAR — switchgear downstream of MV/LV xfmr, on the 480 V LV bus
    def test_lv_switchgear_closes_when_fed(self, engine):
        parent = _node("p", "MV_LV_TRANSFORMER", v_out=480.0)
        n = _node("sw", "LV_SWITCHGEAR", parent_id="p")
        v, s = engine._compute_node(n, {"p": parent, "sw": n})
        assert v == 480.0 and s == "CLOSED"

    @pytest.mark.parametrize("sticky", ["TRIPPED", "FAULT"])
    def test_lv_switchgear_sticky_faults_survive_re_energise(self, engine, sticky):
        parent = _node("p", "MV_LV_TRANSFORMER", v_out=480.0)
        n = _node("sw", "LV_SWITCHGEAR", parent_id="p", status_msg=sticky)
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

    # LV_SWITCHGEAR transfer point — prefers the utility path (parent = MV/LV
    # transformer) over the generator (secondary). Absorbed the former ATS role.
    def test_lv_switchgear_on_utility_path(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=480.0)
        gen  = _node("gen",  "GENERATOR",         v_out=480.0)
        sw   = _node("lv_switchgear_a", "LV_SWITCHGEAR",
                     parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(sw, {"xfmr": prim, "gen": gen, "lv_switchgear_a": sw})
        assert (v, s) == (480.0, "CLOSED")

    def test_lv_switchgear_transfers_to_generator(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=0.0)
        gen  = _node("gen",  "GENERATOR",         v_out=480.0)
        sw   = _node("lv_switchgear_a", "LV_SWITCHGEAR",
                     parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(sw, {"xfmr": prim, "gen": gen, "lv_switchgear_a": sw})
        assert (v, s) == (480.0, "GENERATOR")

    def test_lv_switchgear_no_input_when_both_sources_dead(self, engine):
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=0.0)
        gen  = _node("gen",  "GENERATOR",         v_out=0.0)
        sw   = _node("lv_switchgear_a", "LV_SWITCHGEAR",
                     parent_id="xfmr", secondary_parent_id="gen")
        v, s = engine._compute_node(sw, {"xfmr": prim, "gen": gen, "lv_switchgear_a": sw})
        assert (v, s) == (0.0, "NO_INPUT")

    def test_lv_switchgear_recloses_on_utility_after_no_input(self, engine):
        """NO_INPUT is not sticky: when the utility path returns the switchgear
        re-closes — this is what lets utility recovery cascade downstream."""
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=480.0)
        sw   = _node("lv_switchgear_a", "LV_SWITCHGEAR",
                     parent_id="xfmr", status_msg="NO_INPUT")
        v, s = engine._compute_node(sw, {"xfmr": prim, "lv_switchgear_a": sw})
        assert (v, s) == (480.0, "CLOSED")

    @pytest.mark.parametrize("sticky", ["OPEN", "TRIPPED", "FAULT"])
    def test_lv_switchgear_sticky_states_block_both_sources(self, engine, sticky):
        """Operator OPEN and protective trips drop the LV bus even when a source
        (utility OR generator) is live."""
        prim = _node("xfmr", "MV_LV_TRANSFORMER", v_out=480.0)
        gen  = _node("gen",  "GENERATOR",         v_out=480.0)
        sw   = _node("lv_switchgear_a", "LV_SWITCHGEAR", parent_id="xfmr",
                     secondary_parent_id="gen", status_msg=sticky)
        v, s = engine._compute_node(sw, {"xfmr": prim, "gen": gen, "lv_switchgear_a": sw})
        assert (v, s) == (0.0, sticky)

    # UPS — charge / discharge state machine. Parent = lv_switchgear (the LV
    # transfer point) now that the ATS node is gone.
    def test_ups_charging_then_normal(self, engine):
        parent = _node("lv_switchgear_a", "LV_SWITCHGEAR", v_out=480.0)
        ups = _node("ups", "UPS", parent_id="lv_switchgear_a", battery_level=98)
        v, s = engine._compute_node(ups, {"lv_switchgear_a": parent, "ups": ups})
        assert (v, s) == (480.0, "CHARGING") and ups["battery_level"] == 99
        v, s = engine._compute_node(ups, {"lv_switchgear_a": parent, "ups": ups})
        assert (v, s) == (480.0, "NORMAL") and ups["battery_level"] == 100
        # already at 100 — clamp, don't overflow
        v, s = engine._compute_node(ups, {"lv_switchgear_a": parent, "ups": ups})
        assert ups["battery_level"] == 100

    def test_ups_on_battery_then_fault(self, engine):
        parent = _node("lv_switchgear_a", "LV_SWITCHGEAR", v_out=0.0)
        ups = _node("ups", "UPS", parent_id="lv_switchgear_a", battery_level=1)
        v, s = engine._compute_node(ups, {"lv_switchgear_a": parent, "ups": ups})
        assert (v, s) == (480.0, "ON_BATTERY") and ups["battery_level"] == 0
        v, s = engine._compute_node(ups, {"lv_switchgear_a": parent, "ups": ups})
        assert (v, s) == (0.0, "FAULT")

    # COOLING — parent = lv_switchgear (mech branch, parallel to the UPS)
    def test_cooling_off_with_no_input(self, engine):
        parent = _node("lv_switchgear_a", "LV_SWITCHGEAR", v_out=0.0)
        n = _node("cool", "COOLING", parent_id="lv_switchgear_a")
        assert engine._compute_node(n, {"lv_switchgear_a": parent, "cool": n}) == (0.0, "OFF")

    # SERVER_RACK — single-fed from this side's UPS (no PATH_A/PATH_B).
    def test_server_rack_normal_when_ups_normal(self, engine):
        ups = _node("ups_a", "UPS", v_out=480.0, status_msg="NORMAL")
        rack = _node("server_rack_a1", "SERVER_RACK", side="A", parent_id="ups_a")
        v, s = engine._compute_node(rack, {"ups_a": ups, "server_rack_a1": rack})
        assert (v, s) == (48.0, "NORMAL")

    def test_server_rack_degraded_when_ups_on_battery(self, engine):
        ups = _node("ups_a", "UPS", v_out=480.0, status_msg="ON_BATTERY")
        rack = _node("server_rack_a1", "SERVER_RACK", side="A", parent_id="ups_a")
        v, s = engine._compute_node(rack, {"ups_a": ups, "server_rack_a1": rack})
        assert (v, s) == (48.0, "DEGRADED")

    def test_server_rack_normal_when_ups_charging(self, engine):
        """Once the side is back on generator/utility power the UPS reads CHARGING;
        racks are fed clean power again and recover to NORMAL while the battery
        recharges in the background (the recharge does not degrade the IT load)."""
        ups = _node("ups_a", "UPS", v_out=480.0, status_msg="CHARGING")
        rack = _node("server_rack_a1", "SERVER_RACK", side="A", parent_id="ups_a")
        v, s = engine._compute_node(rack, {"ups_a": ups, "server_rack_a1": rack})
        assert (v, s) == (48.0, "NORMAL")

    def test_server_rack_fault_without_ups_feed(self, engine):
        ups = _node("ups_a", "UPS", v_out=0.0, status_msg="FAULT")
        rack = _node("server_rack_a1", "SERVER_RACK", side="A", parent_id="ups_a")
        v, s = engine._compute_node(rack, {"ups_a": ups, "server_rack_a1": rack})
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

    def test_lv_switchgear_close_vs_open(self, engine):
        n = _node("sw", "LV_SWITCHGEAR")
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
        assert "INPUT:480.0" in out   # fed (CHARGING) → input live

    def test_ups_input_zero_on_battery(self, engine):
        """Islanding on battery: the firmware-facing INPUT token must read 0 even
        though the UPS still outputs 480 V to the racks downstream."""
        n = _node("u", "UPS", battery_level=50)
        out = engine._control_cmd(n, 480.0, "ON_BATTERY")
        assert "INPUT:0.0" in out and "STATUS:ON_BATTERY" in out

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
        n = _node("rack", "SERVER_RACK", side="A")
        out = engine._control_cmd(n, 0.0, "FAULT")
        assert "TEMP" not in out

    def test_server_rack_includes_hot_aisle_when_finite(self, engine):
        engine._latest_thermal = {
            "mode": "NORMAL", "hot_aisle_f": 94.7,
            "cold_aisle_f": 84.1, "flow_pct_max": 11.0,
        }
        n = _node("rack", "SERVER_RACK", side="A")
        out = engine._control_cmd(n, 48.0, "NORMAL")
        # Single-fed: no PATH_A / PATH_B at the rack any more.
        assert "PATH_A" not in out and "PATH_B" not in out
        assert "TEMP:94.7" in out and "INPUT:480.0" in out


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
    @pytest.mark.parametrize("node_id", ["facility", "weather"])
    def test_virtual_status_messages_bypass_db_validation(self, ingest_engine, node_id):
        msg = _make_msg(f"winter-river/{node_id}/status", '{"status":"ONLINE"}')
        ingest_engine.on_message(None, None, msg)
        assert ingest_engine._exec_log == []
        ingest_engine.db.commit.assert_not_called()

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


# ── weather MQTT control ──────────────────────────────────────────────────────

def _weather_msg(payload, retain=False):
    """Build a winter-river/weather/control message. `retain` is set explicitly
    because a bare MagicMock attribute is truthy — which would make every message
    look retained to _handle_weather_control."""
    msg = MagicMock()
    msg.topic = "winter-river/weather/control"
    msg.payload = payload if isinstance(payload, (bytes, bytearray)) \
        else payload.encode()
    msg.retain = retain
    return msg


@pytest.fixture
def weather_engine():
    """Engine with only the attrs the weather-control path touches. The DB cursor
    is wired up so tests can assert weather control performs NO database I/O."""
    eng = WinterRiverEngine.__new__(WinterRiverEngine)
    eng._thermal_cfg = ThermalConfig()
    eng._weather = resolve_weather({"preset": 1})
    eng.mqtt_client = MagicMock()
    eng._known_nodes = {"utility_a", "cooling_a"}
    eng._exec_log = []
    eng.db = MagicMock()
    eng.db.cursor = lambda: _FakeCursor(eng._exec_log, eng._known_nodes)
    return eng


@pytest.fixture
def constructed_engine(monkeypatch):
    """Run the real __init__ with MQTT / Postgres / InfluxDB stubbed out, so we
    can assert on actual startup wiring (default weather + subscriptions)."""
    fake_client = MagicMock()
    monkeypatch.setattr(broker_main.mqtt, "Client", lambda *a, **k: fake_client)
    monkeypatch.setattr(
        broker_main.psycopg2, "connect",
        MagicMock(side_effect=broker_main.psycopg2.OperationalError("no db")),
    )
    monkeypatch.setattr(broker_main, "HAS_INFLUX", False)
    return WinterRiverEngine()


class TestWeatherStartup:
    def test_default_weather_is_preset_1(self, constructed_engine):
        w = constructed_engine._weather
        assert w["preset"] == 1
        assert w["name"] == "Virginia Summer"
        assert "custom" not in w

    def test_on_connect_subscribes_status_and_weather_control(self, constructed_engine):
        client = MagicMock()
        constructed_engine._on_mqtt_connect(client, None, None, 0)
        subscribed = [c.args[0] for c in client.subscribe.call_args_list]
        assert "winter-river/+/status" in subscribed
        assert "winter-river/weather/control" in subscribed

    def test_on_connect_failure_subscribes_nothing(self, constructed_engine):
        client = MagicMock()
        constructed_engine._on_mqtt_connect(client, None, None, 5)  # rc != 0
        client.subscribe.assert_not_called()


class TestWeatherControl:
    def test_preset_4_updates_weather_and_publishes_retained(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("PRESET:4"))
        w = weather_engine._weather
        assert w["preset"] == 4
        assert w["name"] == "Arizona Summer"
        assert w["outdoor_f"] == pytest.approx(109.4)
        assert "custom" not in w

        pub = weather_engine.mqtt_client.publish
        pub.assert_called_once()
        assert pub.call_args.args[0] == "winter-river/weather/status"
        assert pub.call_args.kwargs.get("retain") is True
        assert pub.call_args.kwargs.get("qos") == 1
        body = json.loads(pub.call_args.args[1])
        assert body["preset"] == 4 and body["name"] == "Arizona Summer"
        assert "custom" not in body

    def test_reset_returns_to_default_preset(self, weather_engine):
        weather_engine._weather = resolve_weather({"preset": 4})
        weather_engine.on_message(None, None, _weather_msg("RESET"))
        w = weather_engine._weather
        assert w["preset"] == 1 and w["name"] == "Virginia Summer"
        assert "custom" not in w

    def test_compound_preset_then_override_marks_custom(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("PRESET:6 RH_PCT:75"))
        w = weather_engine._weather
        assert w["preset"] == 6 and w["name"] == "Singapore Monsoon"
        assert w["rh_pct"] == 75.0
        assert w["outdoor_f"] == pytest.approx(91.4)  # preset's outdoor retained
        assert w["custom"] is True
        body = json.loads(weather_engine.mqtt_client.publish.call_args.args[1])
        assert body["custom"] is True and body["rh_pct"] == 75.0

    def test_outdoor_f_override_marks_custom(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("OUTDOOR_F:72.5"))
        w = weather_engine._weather
        assert w["outdoor_f"] == 72.5 and w["custom"] is True
        assert w["preset"] == 1  # preset itself unchanged

    def test_rh_pct_override_is_clamped(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("RH_PCT:150"))
        assert weather_engine._weather["rh_pct"] == 100.0
        weather_engine.on_message(None, None, _weather_msg("RH_PCT:-20"))
        assert weather_engine._weather["rh_pct"] == 0.0

    def test_preset_clears_prior_custom_override(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("OUTDOOR_F:50"))
        assert weather_engine._weather["custom"] is True
        weather_engine.on_message(None, None, _weather_msg("PRESET:5"))
        w = weather_engine._weather
        assert w["preset"] == 5 and "custom" not in w
        assert w["outdoor_f"] == pytest.approx(33.8)  # Stockholm; override gone

    @pytest.mark.parametrize("cmd", [
        "PRESET:9", "PRESET:0", "PRESET:abc", "OUTDOOR_F:hot",
        "RH_PCT:wet", "GARBAGE", "FOO:1", "PRESET:4 OUTDOOR_F:nope",
    ])
    def test_invalid_command_leaves_weather_unchanged(self, weather_engine, cmd):
        before = dict(weather_engine._weather)
        weather_engine.on_message(None, None, _weather_msg(cmd))
        assert weather_engine._weather == before
        weather_engine.mqtt_client.publish.assert_not_called()

    def test_bad_utf8_payload_leaves_weather_unchanged(self, weather_engine):
        before = dict(weather_engine._weather)
        weather_engine.on_message(None, None, _weather_msg(b"\xff\xfe PRESET:4"))
        assert weather_engine._weather == before
        weather_engine.mqtt_client.publish.assert_not_called()

    def test_empty_payload_is_noop(self, weather_engine):
        before = dict(weather_engine._weather)
        weather_engine.on_message(None, None, _weather_msg("   "))
        assert weather_engine._weather == before
        weather_engine.mqtt_client.publish.assert_not_called()

    def test_retained_weather_control_is_ignored(self, weather_engine):
        before = dict(weather_engine._weather)
        weather_engine.on_message(None, None, _weather_msg("PRESET:4", retain=True))
        assert weather_engine._weather == before
        weather_engine.mqtt_client.publish.assert_not_called()

    def test_weather_control_bypasses_db_and_writes_nothing(self, weather_engine):
        weather_engine.on_message(None, None, _weather_msg("PRESET:3"))
        assert weather_engine._weather["preset"] == 3
        # Routed before any node validation — no live_status / historical_data I/O.
        assert weather_engine._exec_log == []
        weather_engine.db.commit.assert_not_called()

    def test_weather_control_works_without_db(self, weather_engine):
        weather_engine.db = None
        weather_engine.on_message(None, None, _weather_msg("PRESET:2"))
        assert weather_engine._weather["preset"] == 2
        assert weather_engine._weather["name"] == "Eastern Oregon Winter"


# ── module-level smoke ────────────────────────────────────────────────────────

def test_module_loads_with_seeded_config():
    """conftest copies config.sample.toml → config.toml when missing, so just
    re-importing should never raise. Guards against accidental drift between
    sample and the keys main.py reads (mqtt.broker_host, database.dsn, etc.).
    """
    assert hasattr(broker_main, "WinterRiverEngine")
    assert broker_main.MQTT_PORT == 1883


def test_load_config_missing_file_explains_pi_setup(monkeypatch, tmp_path):
    missing_cfg = tmp_path / "config.toml"
    monkeypatch.setenv("WINTER_RIVER_CONFIG", str(missing_cfg))

    with pytest.raises(SystemExit) as excinfo:
        broker_main._load_config()

    msg = str(excinfo.value)
    assert "Winter River broker config not found" in msg
    assert "cp config.sample.toml config.toml" in msg
    assert "git-ignored" in msg


def test_load_config_rejects_legacy_schema(monkeypatch, tmp_path):
    legacy_cfg = tmp_path / "config.toml"
    legacy_cfg.write_text(
        """
[mqtt]
host = "192.168.4.1"
port = 1883

[database]
host = "localhost"
port = 5432
database = "winter_river"
user = "postgres"
password = "changeme"
""".lstrip(),
        encoding="utf-8",
    )
    monkeypatch.setenv("WINTER_RIVER_CONFIG", str(legacy_cfg))

    with pytest.raises(SystemExit) as excinfo:
        broker_main._load_config()

    msg = str(excinfo.value)
    assert "mqtt.broker_host" in msg
    assert "mqtt.broker_port" in msg
    assert "database.dsn" in msg


def test_resolve_influx_token_prefers_configured_env(monkeypatch):
    for name in broker_main.INFLUX_TOKEN_ENV_VARS:
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("WR_TEST_INFLUX_TOKEN", "from-env")
    assert broker_main._resolve_influx_token({
        "token_env": "WR_TEST_INFLUX_TOKEN",
        "token": "from-config",
    }) == "from-env"


def test_resolve_influx_token_checks_setup_env_names(monkeypatch):
    for name in broker_main.INFLUX_TOKEN_ENV_VARS:
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("INFLUXDB_ADMIN_TOKEN", "from-grafana-env")
    assert broker_main._resolve_influx_token({"token": "from-config"}) == \
        "from-grafana-env"


def test_resolve_influx_token_falls_back_to_config(monkeypatch):
    for name in broker_main.INFLUX_TOKEN_ENV_VARS:
        monkeypatch.delenv(name, raising=False)
    assert broker_main._resolve_influx_token({"token": "from-config"}) == \
        "from-config"
