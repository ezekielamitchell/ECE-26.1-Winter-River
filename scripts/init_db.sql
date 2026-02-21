-- Topology: Defines who is plugged into whom 
CREATE TABLE nodes ( 
    node_id VARCHAR(50) PRIMARY KEY, 
    node_type VARCHAR(20) NOT NULL,  
    side CHAR(1),          -- A or B
    parent_id VARCHAR(50) REFERENCES nodes(node_id), 
    v_ratio FLOAT DEFAULT 1.0        
);

-- Live State: The current "Digital Twin" status 
CREATE TABLE live_status ( 
    node_id VARCHAR(50) PRIMARY KEY REFERENCES nodes(node_id), 
    is_present BOOLEAN DEFAULT FALSE, 
    v_in FLOAT DEFAULT 0.0, 
    v_out FLOAT DEFAULT 0.0, 
    status_msg VARCHAR(50) DEFAULT 'OFFLINE',
    battery_level INT DEFAULT 100,
    gen_timer INT DEFAULT 0,
    last_update TIMESTAMP DEFAULT NOW()
);

-- Historical Data: For tracking changes over time
CREATE TABLE historical_data ( 
    id SERIAL PRIMARY KEY, 
    node_id VARCHAR(50) REFERENCES nodes(node_id), 
    timestamp TIMESTAMP DEFAULT NOW(),
);

-- Seed Data for Step 2 
INSERT INTO nodes (node_id, node_type, side,parent_id, v_ratio) VALUES  
('util_a', 'UTILITY', 'A', NULL, 1.0),
('trf_a',  'TRANSFORMER', 'A', 'util_a', 0.15),
('sw_a',   'SW_GEAR', 'A', 'trf_a', 0.014),
('gen_a',  'GENERATOR', 'A', NULL, 1.0),
('dist_a', 'DIST_BOARD', 'A', 'sw_a', 1.0),
('ups_a',  'UPS', 'A', 'dist_a', 1.0),
('pdu_a',  'PDU', 'A', 'ups_a', 1.0),
('srv_a',  'SERVER_RACK', 'A', 'pdu_a', 0.1),

('util_b', 'UTILITY', 'B', NULL, 1.0),
('trf_b',  'TRANSFORMER', 'B', 'util_b', 0.15),
('sw_b',   'SW_GEAR', 'B', 'trf_b', 0.014),
('gen_b',  'GENERATOR', 'B', NULL, 1.0),
('dist_b', 'DIST_BOARD', 'B', 'sw_b', 1.0),
('ups_b',  'UPS', 'B', 'dist_b', 1.0),
('pdu_b',  'PDU', 'B', 'ups_b', 1.0),
('srv_b',  'SERVER_RACK', 'B', 'pdu_b', 0.1);

INSERT INTO live_status (node_id) SELECT node_id FROM nodes;
