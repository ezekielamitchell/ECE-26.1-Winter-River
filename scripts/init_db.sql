-- Winter River topology (Side A + Side B, fully independent block-redundant 2N)
-- Each side is a complete power chain feeding 4 server racks. Sides do not
-- share a rectifier or any other power node — side-A failure kills all 4 of
-- side-A's racks. Redundancy is at the side (block) level, not per-rack.
--
-- Chain per side:
--   utility → hv_switchgear → hv_mv_transformer → mv_switchgear
--   → mv_lv_transformer; generator ↗ ats (LV transfer switch) → ups
--   → server_rack_{1..4};   ats ↘ cooling (mech load, parallel to ups)
--
-- Total: 26 active broker/DB nodes = 13 per side × 2 sides.
-- `bms` is broker-published only (NOT seeded here); broker/main.py synthesizes
-- winter-river/bms/status from live node state every tick.
--
-- Run as: psql -U postgres -d winter_river -f scripts/init_db.sql

-- ── SCHEMA ────────────────────────────────────────────────────────────────────

-- Static topology: who is plugged into whom
-- secondary_parent_id handles ATS dual feed (transformer + generator).
CREATE TABLE nodes (
    node_id              VARCHAR(50) PRIMARY KEY,
    node_type            VARCHAR(30) NOT NULL,
    -- UTILITY | HV_SWITCHGEAR | HV_MV_TRANSFORMER | MV_SWITCHGEAR |
    -- MV_LV_TRANSFORMER | GENERATOR | ATS | UPS | COOLING | SERVER_RACK
    side                 CHAR(1),           -- 'A' or 'B' (no shared nodes)
    parent_id            VARCHAR(50) REFERENCES nodes(node_id),
    secondary_parent_id  VARCHAR(50) REFERENCES nodes(node_id),
    -- ATS only: primary = transformer path, secondary = generator
    rated_voltage        FLOAT DEFAULT 480.0,  -- nominal output voltage (V)
    v_ratio              FLOAT DEFAULT 1.0     -- step-down ratio (metadata only)
);

-- Live "digital twin" state — updated every simulation tick
CREATE TABLE live_status (
    node_id        VARCHAR(50) PRIMARY KEY REFERENCES nodes(node_id),
    is_present     BOOLEAN DEFAULT FALSE,
    v_in           FLOAT DEFAULT 0.0,
    v_out          FLOAT DEFAULT 0.0,
    status_msg     VARCHAR(50) DEFAULT 'OFFLINE',
    battery_level  INT DEFAULT 100,   -- UPS only (0–100 %)
    gen_timer      INT DEFAULT 10,    -- GENERATOR startup countdown (ticks)
    last_update    TIMESTAMP DEFAULT NOW()
);

-- Historical telemetry log — raw JSON from ESP32 MQTT messages
CREATE TABLE historical_data (
    id        SERIAL PRIMARY KEY,
    node_id   VARCHAR(50) REFERENCES nodes(node_id),
    timestamp TIMESTAMP DEFAULT NOW(),
    metrics   JSONB
);

-- Computed facility metrics from broker/thermal.py — one row per simulation tick
-- when thermal output is published (winter-river/facility/status).
CREATE TABLE facility_metrics (
    id              SERIAL PRIMARY KEY,
    timestamp       TIMESTAMP DEFAULT NOW(),
    mode            VARCHAR(30),
    outdoor_f       FLOAT,
    rh_pct          FLOAT,
    cold_aisle_f    FLOAT,
    hot_aisle_f     FLOAT,
    pue             FLOAT,
    p_data_w        FLOAT,
    p_fan_w         FLOAT,
    p_loss_w        FLOAT,
    p_consumption_w FLOAT,
    q_cfm           FLOAT,
    fan_pct_max     FLOAT,
    flow_pct_max    FLOAT,
    rack_dp_pa      FLOAT,
    fan_dp_pa       FLOAT,
    fan_count       INT
);
CREATE INDEX facility_metrics_ts_idx ON facility_metrics(timestamp DESC);

-- ── SEED DATA: SIDE A ─────────────────────────────────────────────────────────
-- IT path:   utility_a → hv_switchgear_a → hv_mv_transformer_a → mv_switchgear_a
--            → mv_lv_transformer_a; generator_a ↗ ats_a → ups_a →
--            server_rack_a{1..4}
-- Mech path: ats_a → cooling_a (parallel to ups_a)

INSERT INTO nodes (node_id, node_type, side, parent_id, secondary_parent_id, rated_voltage, v_ratio) VALUES
-- ① Root: 230 kV utility grid
('utility_a',           'UTILITY',           'A', NULL,                     NULL,          230000.0, 1.0),
-- ② HV switchgear: 230 kV main breaker (first on-site protection)
('hv_switchgear_a',     'HV_SWITCHGEAR',     'A', 'utility_a',              NULL,          230000.0, 1.0),
-- ③ HV/MV transformer: 230 kV → 34.5 kV
('hv_mv_transformer_a', 'HV_MV_TRANSFORMER', 'A', 'hv_switchgear_a',        NULL,           34500.0, 0.15),
-- ④ MV switchgear: 34.5 kV bus
('mv_switchgear_a',     'MV_SWITCHGEAR',     'A', 'hv_mv_transformer_a',    NULL,           34500.0, 1.0),
-- ⑤ Step-down transformer: 34.5 kV → 480 V
('mv_lv_transformer_a', 'MV_LV_TRANSFORMER', 'A', 'mv_switchgear_a',        NULL,             480.0, 0.0139),
-- ⑥ Diesel generator: 480 V backup (no parent — autonomous source)
('generator_a',         'GENERATOR',         'A', NULL,                     NULL,             480.0, 1.0),
-- ⑦ ATS (LV transfer switch): primary = transformer path, secondary = generator
('ats_a',               'ATS',               'A', 'mv_lv_transformer_a',    'generator_a',    480.0, 1.0),
-- ⑧ UPS: 480 V + battery backup, feeds the IT racks
('ups_a',               'UPS',               'A', 'ats_a',                  NULL,             480.0, 1.0),
-- ⑨ Cooling: 480 V mech load, branches off ats_a (parallel to ups_a)
('cooling_a',           'COOLING',           'A', 'ats_a',                  NULL,             480.0, 1.0),
-- ⑩-⑬ Four server racks, each single-fed from ups_a (480 V AC → 48 V DC inside)
('server_rack_a1',      'SERVER_RACK',       'A', 'ups_a',                  NULL,              48.0, 0.1),
('server_rack_a2',      'SERVER_RACK',       'A', 'ups_a',                  NULL,              48.0, 0.1),
('server_rack_a3',      'SERVER_RACK',       'A', 'ups_a',                  NULL,              48.0, 0.1),
('server_rack_a4',      'SERVER_RACK',       'A', 'ups_a',                  NULL,              48.0, 0.1);

-- ── SEED DATA: SIDE B (mirror of Side A) ─────────────────────────────────────

INSERT INTO nodes (node_id, node_type, side, parent_id, secondary_parent_id, rated_voltage, v_ratio) VALUES
('utility_b',           'UTILITY',           'B', NULL,                     NULL,          230000.0, 1.0),
('hv_switchgear_b',     'HV_SWITCHGEAR',     'B', 'utility_b',              NULL,          230000.0, 1.0),
('hv_mv_transformer_b', 'HV_MV_TRANSFORMER', 'B', 'hv_switchgear_b',        NULL,           34500.0, 0.15),
('mv_switchgear_b',     'MV_SWITCHGEAR',     'B', 'hv_mv_transformer_b',    NULL,           34500.0, 1.0),
('mv_lv_transformer_b', 'MV_LV_TRANSFORMER', 'B', 'mv_switchgear_b',        NULL,             480.0, 0.0139),
('generator_b',         'GENERATOR',         'B', NULL,                     NULL,             480.0, 1.0),
('ats_b',               'ATS',               'B', 'mv_lv_transformer_b',    'generator_b',    480.0, 1.0),
('ups_b',               'UPS',               'B', 'ats_b',                  NULL,             480.0, 1.0),
('cooling_b',           'COOLING',           'B', 'ats_b',                  NULL,             480.0, 1.0),
('server_rack_b1',      'SERVER_RACK',       'B', 'ups_b',                  NULL,              48.0, 0.1),
('server_rack_b2',      'SERVER_RACK',       'B', 'ups_b',                  NULL,              48.0, 0.1),
('server_rack_b3',      'SERVER_RACK',       'B', 'ups_b',                  NULL,              48.0, 0.1),
('server_rack_b4',      'SERVER_RACK',       'B', 'ups_b',                  NULL,              48.0, 0.1);

-- ── INITIALISE live_status FOR ALL NODES ──────────────────────────────────────
INSERT INTO live_status (node_id) SELECT node_id FROM nodes;
