-- Winter River 24-node topology (25 devices total including shared server_rack)
-- 2N redundant: Side A (12 nodes) + Side B (12 nodes) + server_rack (2N shared)
-- Run as: psql -U postgres -d winter_river -f scripts/init_db.sql

-- ── SCHEMA ────────────────────────────────────────────────────────────────────

-- Static topology: who is plugged into whom
-- secondary_parent_id handles ATS (dual feed: transformer + generator)
-- and server_rack (dual feed: rectifier_a + rectifier_b)
CREATE TABLE nodes (
    node_id              VARCHAR(50) PRIMARY KEY,
    node_type            VARCHAR(30) NOT NULL,
    -- UTILITY | MV_SWITCHGEAR | MV_LV_TRANSFORMER | GENERATOR |
    -- ATS | LV_DIST | UPS | PDU | RECTIFIER | COOLING | LIGHTING |
    -- MONITORING | SERVER_RACK
    side                 CHAR(1),           -- 'A', 'B', or NULL (shared)
    parent_id            VARCHAR(50) REFERENCES nodes(node_id),
    secondary_parent_id  VARCHAR(50) REFERENCES nodes(node_id),
    -- ATS: primary=transformer path, secondary=generator
    -- SERVER_RACK: primary=rectifier_a, secondary=rectifier_b
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

-- ── SEED DATA: SIDE A ─────────────────────────────────────────────────────────
-- Chain (IT path): utility_a → mv_switchgear_a → mv_lv_transformer_a
--                  generator_a ↗ ats_a → lv_dist_a → ups_a → pdu_a → rectifier_a → server_rack
-- Facility branches off lv_dist_a: cooling_a, lighting_a, monitoring_a

INSERT INTO nodes (node_id, node_type, side, parent_id, secondary_parent_id, rated_voltage, v_ratio) VALUES
-- ① Root: 230 kV utility grid
('utility_a',           'UTILITY',           'A', NULL,                  NULL,          230000.0, 1.0),
-- ② MV switchgear: 230 kV → passes through at 34.5 kV bus
('mv_switchgear_a',     'MV_SWITCHGEAR',     'A', 'utility_a',           NULL,           34500.0, 0.15),
-- ③ Step-down transformer: 34.5 kV → 480 V
('mv_lv_transformer_a', 'MV_LV_TRANSFORMER', 'A', 'mv_switchgear_a',     NULL,             480.0, 0.0139),
-- ④ Diesel generator: 480 V backup (no parent — autonomous source)
('generator_a',         'GENERATOR',         'A', NULL,                  NULL,             480.0, 1.0),
-- ⑤ ATS: primary = transformer path, secondary = generator
('ats_a',               'ATS',               'A', 'mv_lv_transformer_a', 'generator_a',    480.0, 1.0),
-- ⑥ LV distribution board: 480 V, feeds IT path + facility loads
('lv_dist_a',           'LV_DIST',           'A', 'ats_a',               NULL,             480.0, 1.0),
-- ⑦ UPS: 480 V + battery backup
('ups_a',               'UPS',               'A', 'lv_dist_a',           NULL,             480.0, 1.0),
-- ⑧ PDU: 480 V power distribution unit
('pdu_a',               'PDU',               'A', 'ups_a',               NULL,             480.0, 1.0),
-- ⑨ Rectifier: 480 V AC → 48 V DC (feeds server_rack)
('rectifier_a',         'RECTIFIER',         'A', 'pdu_a',               NULL,              48.0, 0.1),
-- ⑩ Cooling: 480 V, branches off lv_dist_a
('cooling_a',           'COOLING',           'A', 'lv_dist_a',           NULL,             480.0, 1.0),
-- ⑪ Lighting: 277 V (phase-to-neutral of 480Y/277V system)
('lighting_a',          'LIGHTING',          'A', 'lv_dist_a',           NULL,             277.0, 0.577),
-- ⑫ Monitoring: 120 V (transformer-derived from 480V bus)
('monitoring_a',        'MONITORING',        'A', 'lv_dist_a',           NULL,             120.0, 0.25);

-- ── SEED DATA: SIDE B ─────────────────────────────────────────────────────────

INSERT INTO nodes (node_id, node_type, side, parent_id, secondary_parent_id, rated_voltage, v_ratio) VALUES
('utility_b',           'UTILITY',           'B', NULL,                  NULL,          230000.0, 1.0),
('mv_switchgear_b',     'MV_SWITCHGEAR',     'B', 'utility_b',           NULL,           34500.0, 0.15),
('mv_lv_transformer_b', 'MV_LV_TRANSFORMER', 'B', 'mv_switchgear_b',     NULL,             480.0, 0.0139),
('generator_b',         'GENERATOR',         'B', NULL,                  NULL,             480.0, 1.0),
('ats_b',               'ATS',               'B', 'mv_lv_transformer_b', 'generator_b',    480.0, 1.0),
('lv_dist_b',           'LV_DIST',           'B', 'ats_b',               NULL,             480.0, 1.0),
('ups_b',               'UPS',               'B', 'lv_dist_b',           NULL,             480.0, 1.0),
('pdu_b',               'PDU',               'B', 'ups_b',               NULL,             480.0, 1.0),
('rectifier_b',         'RECTIFIER',         'B', 'pdu_b',               NULL,              48.0, 0.1),
('cooling_b',           'COOLING',           'B', 'lv_dist_b',           NULL,             480.0, 1.0),
('lighting_b',          'LIGHTING',          'B', 'lv_dist_b',           NULL,             277.0, 0.577),
('monitoring_b',        'MONITORING',        'B', 'lv_dist_b',           NULL,             120.0, 0.25);

-- ── SEED DATA: SHARED 2N NODE ─────────────────────────────────────────────────
-- server_rack is fed by BOTH rectifier_a (primary) and rectifier_b (secondary).
-- NORMAL: both paths live. DEGRADED: one path only. FAULT: both paths dead.

INSERT INTO nodes (node_id, node_type, side, parent_id, secondary_parent_id, rated_voltage, v_ratio) VALUES
('server_rack', 'SERVER_RACK', NULL, 'rectifier_a', 'rectifier_b', 48.0, 1.0);

-- ── INITIALISE live_status FOR ALL NODES ──────────────────────────────────────
INSERT INTO live_status (node_id) SELECT node_id FROM nodes;
