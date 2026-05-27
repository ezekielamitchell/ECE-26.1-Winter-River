"""Winter River thermal & airflow model.

Computes hot-aisle temperature, PUE, airflow, and pressure across the
fleet given outdoor weather, server module mix, and fan count. Implements
the Capstone Model reference, including the underpressure correction loop
that ramps fan power 10 kW at a time until rack pressure clears.

Pure functions. SI units internally; Fahrenheit / cfm only at the boundary.
Drive from broker/main.py — no I/O, no MQTT, no DB here.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, Mapping, Optional


_M3S_TO_CFM = 2118.88
_CFM_TO_M3S = 0.00047194745
_CP_AIR = 1005.0
_P_ATM = 101325.0
_EPS = 0.62198
_HFG = 2_501_000.0
_CPV = 1860.0

# Rack pressure-drop coefficient — Capstone Model.py:
#   PrLossRack = 2.4253 * (q_m3s / (Ndata * rackpermod))**2  [Pa]
_RACK_DP_COEFF = 2.4253

# Duct pressure-drop coefficient — Capstone Model.py:
#   PrLossDuct = q_m3s**2 / 8.91e4  [Pa]
_DUCT_DP_DIVISOR = 8.91e4

# Underpressure correction loop step size (W). Capstone ramps fan power
# 10 kW at a time until rack pressure clears or fans hit max.
_BOOST_STEP_W = 10_000.0


def f_to_c(f: float) -> float:
    return (f - 32.0) * 5.0 / 9.0


def c_to_f(c: float) -> float:
    return c * 9.0 / 5.0 + 32.0


@dataclass
class ThermalConfig:
    rho: float = 1.177
    facility_w: float = 500_000.0
    p_max_per_fan_w: float = 80_000.0
    fans_per_module: int = 55
    fan_modules: int = 2
    fan_diff: int = 0
    pr_fan_max_pa: float = 3.25
    q_max_per_fan_m3s: float = 47.57

    servers_per_module: int = 8000
    servers_per_rack: int = 40

    p_per_standard_w: float = 500.0
    p_per_storage_w: float = 800.0
    p_per_ai_w: float = 2500.0

    standard_modules: int = 1
    storage_modules: int = 0
    ai_modules: int = 0

    rh_target: float = 0.80
    free_cooling_floor_c: float = 18.0
    overheat_threshold_f: float = 120.0
    underpressure_threshold_pa: float = 20.0
    loss_fraction: float = 0.08

    @classmethod
    def from_mapping(cls, cfg: Optional[Mapping]) -> "ThermalConfig":
        if not cfg:
            return cls()
        known = {f.name for f in cls.__dataclass_fields__.values()}
        return cls(**{k: v for k, v in cfg.items() if k in known})


def _saturation_vapor_pressure_pa(t_c: float) -> float:
    return 610.94 * math.exp((17.625 * t_c) / (t_c + 243.04))


def _humidity_ratio(t_c: float, rh: float) -> float:
    pw = rh * _saturation_vapor_pressure_pa(t_c)
    return _EPS * pw / (_P_ATM - pw)


def _enthalpy_jkg(t_c: float, w: float) -> float:
    return _CP_AIR * t_c + w * (_HFG + _CPV * t_c)


def evap_outlet_c(t_in_c: float, rh_in: float, rh_target: float = 0.80) -> float:
    """Adiabatic-saturation outlet temperature: solve h(T_out, RH_target) = h(T_in, RH_in)."""
    rh_in = max(0.0, min(rh_in, 1.0))
    w_in = _humidity_ratio(t_in_c, rh_in)
    h_in = _enthalpy_jkg(t_in_c, w_in)

    lo, hi = -20.0, t_in_c
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if _enthalpy_jkg(mid, _humidity_ratio(mid, rh_target)) > h_in:
            hi = mid
        else:
            lo = mid
    return 0.5 * (lo + hi)


WEATHER_PRESETS: Dict[int, Dict] = {
    1: {"name": "Virginia Summer",        "outdoor_f": 95.0,  "rh_pct": 50.0},
    2: {"name": "Eastern Oregon Winter",  "outdoor_f": 41.0,  "rh_pct": 45.0},
    3: {"name": "Ohio Spring",            "outdoor_f": 59.0,  "rh_pct": 60.0},
    4: {"name": "Arizona Summer",         "outdoor_f": 109.4, "rh_pct": 20.0},
    5: {"name": "Stockholm Winter",       "outdoor_f": 33.8,  "rh_pct": 89.0},
    6: {"name": "Singapore Monsoon",      "outdoor_f": 91.4,  "rh_pct": 79.0},
}


def resolve_weather(cfg: Mapping) -> Dict:
    """Return {name, outdoor_f, rh_pct} from a [weather] config block.
    Honors explicit outdoor_f/rh_pct overrides; otherwise falls back to preset.
    """
    preset = int(cfg.get("preset", 1)) if cfg else 1
    base = WEATHER_PRESETS.get(preset, WEATHER_PRESETS[1]).copy()
    if cfg:
        if "outdoor_f" in cfg:
            base["outdoor_f"] = float(cfg["outdoor_f"])
        if "rh_pct" in cfg:
            base["rh_pct"] = float(cfg["rh_pct"])
        if "name" in cfg:
            base["name"] = str(cfg["name"])
    base["preset"] = preset
    return base


def compute_thermal(
    outdoor_f: float,
    rh_pct: float,
    cfg: ThermalConfig,
    *,
    fan_count_override: Optional[int] = None,
    cooling_online: bool = True,
) -> Dict:
    outdoor_c = f_to_c(outdoor_f)
    rh = max(0.0, min(rh_pct, 100.0)) / 100.0

    cold_aisle_c = max(
        evap_outlet_c(outdoor_c, rh, cfg.rh_target),
        cfg.free_cooling_floor_c,
    )

    if fan_count_override is not None:
        n_fans = max(0, int(fan_count_override))
    else:
        n_fans = cfg.fan_modules * cfg.fans_per_module + cfg.fan_diff
    if not cooling_online:
        n_fans = 0
    n_fans = max(0, n_fans)

    p_fan_max = cfg.p_max_per_fan_w * n_fans
    q_fan_max = cfg.q_max_per_fan_m3s * n_fans

    n_std  = max(0, cfg.standard_modules)
    n_stor = max(0, cfg.storage_modules)
    n_ai   = max(0, cfg.ai_modules)
    n_data = n_std + n_stor + n_ai
    racks_per_module = cfg.servers_per_module / cfg.servers_per_rack

    p_std  = n_std  * cfg.servers_per_module * cfg.p_per_standard_w
    p_stor = n_stor * cfg.servers_per_module * cfg.p_per_storage_w
    p_ai   = n_ai   * cfg.servers_per_module * cfg.p_per_ai_w
    p_data = p_std + p_stor + p_ai

    nominal_fans = cfg.fan_modules * cfg.fans_per_module
    if n_fans > 0 and nominal_fans > 0:
        # 160 cfm/kW baseline at nominal fan count, scaled linearly with n_fans
        qvsp_m3s_per_w = (160.0 * _CFM_TO_M3S * n_fans / nominal_fans) / 1000.0
    else:
        qvsp_m3s_per_w = 0.0

    q = qvsp_m3s_per_w * p_data
    flow_capped = q_fan_max > 0 and q > q_fan_max
    q = min(q, q_fan_max) if q_fan_max > 0 else 0.0

    p_fan = min(p_fan_max * (q / q_fan_max) ** 3, p_fan_max) if q_fan_max > 0 else 0.0

    if n_data > 0 and racks_per_module > 0 and q > 0:
        rack_dp = _RACK_DP_COEFF * (q / (n_data * racks_per_module)) ** 2
    else:
        rack_dp = 0.0

    # Underpressure correction loop (Capstone Model.py lines 277-283).
    # Ramp fan power in _BOOST_STEP_W steps until pressure clears or
    # fans hit max. Downstream values (m_total, hot_aisle, fan_dp,
    # duct_loss, p_loss, PUE) are recomputed below using post-boost
    # q / p_fan so the published facility state stays self-consistent.
    boost_applied = False
    if n_data > 0 and racks_per_module > 0 and q_fan_max > 0 and p_fan_max > 0:
        while rack_dp < cfg.underpressure_threshold_pa and p_fan < p_fan_max:
            p_fan = min(p_fan + _BOOST_STEP_W, p_fan_max)
            q = q_fan_max * (p_fan / p_fan_max) ** (1.0 / 3.0)
            rack_dp = _RACK_DP_COEFF * (q / (n_data * racks_per_module)) ** 2
            boost_applied = True

    p_loss = (p_data + cfg.facility_w + p_fan) * cfg.loss_fraction
    p_consumption = p_fan + p_loss + p_data + cfg.facility_w

    m_total = q * cfg.rho
    if m_total > 0 and p_data > 0:
        delta_t_c = p_data / (m_total * _CP_AIR)
    elif p_data > 0:
        delta_t_c = float("inf")
    else:
        delta_t_c = 0.0
    hot_aisle_c = cold_aisle_c + delta_t_c

    if q > 0 and q_fan_max > 0:
        fan_dp = (cfg.pr_fan_max_pa * n_fans) / ((q_fan_max / q) ** 2)
    else:
        fan_dp = 0.0
    duct_loss = (q ** 2) / _DUCT_DP_DIVISOR if q > 0 else 0.0
    exit_pa = fan_dp - rack_dp - duct_loss

    hot_f = c_to_f(hot_aisle_c) if math.isfinite(hot_aisle_c) else float("inf")

    if not cooling_online or n_fans == 0:
        mode = "FAULT"
    elif p_data == 0:
        mode = "IDLE"
    elif hot_f > cfg.overheat_threshold_f:
        mode = "OVERHEATING"
    elif rack_dp < cfg.underpressure_threshold_pa:
        mode = "UNDERPRESSURED"
    else:
        mode = "NORMAL"

    pue = (p_consumption / p_data) if p_data > 0 else 0.0

    return {
        "mode": mode,
        "outdoor_f": outdoor_f,
        "outdoor_c": outdoor_c,
        "rh_pct": rh_pct,
        "cold_aisle_f": c_to_f(cold_aisle_c),
        "cold_aisle_c": cold_aisle_c,
        "hot_aisle_f": hot_f,
        "hot_aisle_c": hot_aisle_c,
        "delta_t_c": delta_t_c,
        "p_data_w": p_data,
        "p_fan_w": p_fan,
        "p_loss_w": p_loss,
        "p_facility_w": cfg.facility_w,
        "p_consumption_w": p_consumption,
        "pue": pue,
        "q_m3s": q,
        "q_cfm": q * _M3S_TO_CFM,
        "fan_count": n_fans,
        "fan_pct_max": (100.0 * p_fan / p_fan_max) if p_fan_max > 0 else 0.0,
        "flow_pct_max": (100.0 * q / q_fan_max) if q_fan_max > 0 else 0.0,
        "flow_capped": flow_capped,
        "boost_applied": boost_applied,
        "rack_dp_pa": rack_dp,
        "fan_dp_pa": fan_dp,
        "duct_loss_pa": duct_loss,
        "exit_pa": exit_pa,
        "racks_total": int(n_data * racks_per_module),
        "modules": {"standard": n_std, "storage": n_stor, "ai": n_ai},
    }
