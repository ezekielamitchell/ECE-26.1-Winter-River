"""Regression tests for broker/thermal.py.

Pinned values come from the psychrometric solver + fan-affinity model at
nominal config (1 standard module, 2 fan modules). Updating any constant
in ThermalConfig will likely shift these — recompute and re-pin
intentionally rather than loosening the tolerances.
"""

import math

import pytest

from thermal import (
    ThermalConfig,
    WEATHER_PRESETS,
    compute_thermal,
    evap_outlet_c,
    resolve_weather,
)


@pytest.fixture
def cfg():
    return ThermalConfig(standard_modules=1)


# ── psychrometrics ────────────────────────────────────────────────────────────

def test_evap_solver_monotonic_in_temperature():
    """Hotter inlet → hotter outlet at fixed RH."""
    a = evap_outlet_c(20.0, 0.5)
    b = evap_outlet_c(35.0, 0.5)
    assert a < b


def test_evap_solver_monotonic_in_humidity():
    """At fixed dry-bulb, more humid air leaves the cooler hotter."""
    dry = evap_outlet_c(35.0, 0.20)
    wet = evap_outlet_c(35.0, 0.80)
    assert dry < wet


# ── presets ───────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("preset, cold_f, hot_f", [
    (1, 84.1,  94.7),   # Virginia Summer
    (2, 64.4,  75.0),   # Oregon Winter — clamped to free-cooling floor
    (3, 64.4,  75.0),   # Ohio Spring   — also at floor
    (4, 79.7,  90.3),   # Arizona Summer
    (5, 64.4,  75.0),   # Stockholm Winter — at floor
    (6, 91.1, 101.7),   # Singapore Monsoon
])
def test_preset_aisle_temps(cfg, preset, cold_f, hot_f):
    """At nominal 1-module load, the underpressure boost dominates flow,
    so hot-aisle delta_T is set by the boosted q (~575 m³/s) rather than
    by IT load. All presets converge to delta_T ≈ 10.6°F above cold aisle.
    """
    p = WEATHER_PRESETS[preset]
    r = compute_thermal(p["outdoor_f"], p["rh_pct"], cfg)
    assert r["cold_aisle_f"] == pytest.approx(cold_f, abs=0.3)
    assert r["hot_aisle_f"]  == pytest.approx(hot_f,  abs=0.5)
    assert r["mode"] == "NORMAL"
    assert r["boost_applied"] is True


def test_free_cooling_floor_clamps_cold_temp(cfg):
    """Outdoor below floor → cold aisle pinned at floor (no over-cooling)."""
    r = compute_thermal(20.0, 80.0, cfg)
    assert r["cold_aisle_f"] == pytest.approx(64.4, abs=0.05)


# ── modes & faults ────────────────────────────────────────────────────────────

def test_cooling_offline_is_fault(cfg):
    r = compute_thermal(95.0, 50.0, cfg, cooling_online=False)
    assert r["mode"] == "FAULT"
    assert r["fan_count"] == 0
    assert math.isinf(r["hot_aisle_f"])


def test_idle_mode_when_no_modules():
    cfg = ThermalConfig(standard_modules=0, storage_modules=0, ai_modules=0)
    r = compute_thermal(95.0, 50.0, cfg)
    assert r["mode"] == "IDLE"
    assert r["pue"] == 0.0
    assert r["p_data_w"] == 0.0


def test_ai_load_normalises_pressure():
    """High AI load increases natural airflow enough to clear underpressure
    without the boost loop firing."""
    cfg = ThermalConfig(ai_modules=2)
    r = compute_thermal(95.0, 50.0, cfg)
    assert r["mode"] == "NORMAL"
    assert r["rack_dp_pa"] >= 20.0
    assert r["boost_applied"] is False


# ── underpressure correction loop (Capstone Model.py lines 277-283) ───────────

def test_boost_clears_underpressure(cfg):
    """Light IT load → natural q is too low → boost ramps fans until
    rack_dp clears the threshold."""
    r = compute_thermal(95.0, 50.0, cfg)
    assert r["boost_applied"] is True
    assert r["rack_dp_pa"] >= cfg.underpressure_threshold_pa
    assert r["mode"] == "NORMAL"


def test_boost_max_out_leaves_underpressured():
    """If pressure can't clear even at p_fan_max, mode stays UNDERPRESSURED."""
    cfg = ThermalConfig(
        standard_modules=1,
        underpressure_threshold_pa=1.0e9,  # unreachable
    )
    r = compute_thermal(95.0, 50.0, cfg)
    assert r["mode"] == "UNDERPRESSURED"
    assert r["fan_pct_max"] == pytest.approx(100.0, abs=0.5)


def test_boost_recomputes_downstream(cfg):
    """Post-boost p_loss, p_consumption, hot_aisle reflect the boosted
    p_fan / q rather than the initial cubic-affinity values."""
    r = compute_thermal(95.0, 50.0, cfg)
    expected_loss = (r["p_data_w"] + r["p_facility_w"] + r["p_fan_w"]) * cfg.loss_fraction
    assert r["p_loss_w"] == pytest.approx(expected_loss, rel=1e-9)
    # m_total derived from boosted q, so delta_T uses post-boost flow.
    m_total = r["q_m3s"] * cfg.rho
    expected_delta_c = r["p_data_w"] / (m_total * 1005.0)
    assert r["delta_t_c"] == pytest.approx(expected_delta_c, rel=1e-9)


# ── PUE & energy bookkeeping ──────────────────────────────────────────────────

def test_pue_above_one(cfg):
    r = compute_thermal(95.0, 50.0, cfg)
    assert r["pue"] > 1.0
    assert r["p_consumption_w"] > r["p_data_w"]


def test_loss_fraction_consistency(cfg):
    r = compute_thermal(95.0, 50.0, cfg)
    expected_loss = (r["p_data_w"] + r["p_facility_w"] + r["p_fan_w"]) * cfg.loss_fraction
    assert r["p_loss_w"] == pytest.approx(expected_loss, rel=1e-9)


# ── fan affinity ──────────────────────────────────────────────────────────────

def test_half_fans_drop_capacity():
    """Halving fan count halves max flow and max power.
    Uses AI load so the pressure boost loop doesn't kick in — the boost
    overshoots by a discrete 10 kW step and the step is proportionally
    larger when q_fan_max is smaller, which inverts q ordering on low load.
    """
    cfg = ThermalConfig(standard_modules=0, ai_modules=2)
    full = compute_thermal(95.0, 50.0, cfg, fan_count_override=110)
    half = compute_thermal(95.0, 50.0, cfg, fan_count_override=55)
    assert half["fan_count"] == 55
    assert full["boost_applied"] is False
    assert half["boost_applied"] is False
    assert full["q_cfm"] > half["q_cfm"]
    assert full["q_cfm"] == pytest.approx(2 * half["q_cfm"], rel=1e-6)


# ── weather config resolver ───────────────────────────────────────────────────

def test_resolve_weather_preset():
    w = resolve_weather({"preset": 4})
    assert w["preset"] == 4
    assert w["outdoor_f"] == pytest.approx(109.4)


def test_resolve_weather_overrides():
    w = resolve_weather({"preset": 1, "outdoor_f": 70.0, "rh_pct": 30.0})
    assert w["outdoor_f"] == 70.0
    assert w["rh_pct"]    == 30.0


def test_resolve_weather_default_when_empty():
    w = resolve_weather({})
    assert w["preset"] == 1
