-- Topology: Defines who is plugged into whom 
CREATE TABLE nodes ( 
    node_id VARCHAR(50) PRIMARY KEY, 
    node_type VARCHAR(20) NOT NULL,  
    parent_id VARCHAR(50) REFERENCES nodes(node_id), 
    v_ratio FLOAT DEFAULT 1.0        
); 

-- Live State: The current "Digital Twin" status 
CREATE TABLE live_status ( 
    node_id VARCHAR(50) PRIMARY KEY REFERENCES nodes(node_id), 
    is_present BOOLEAN DEFAULT FALSE, 
    v_in FLOAT DEFAULT 0.0, 
    v_out FLOAT DEFAULT 0.0, 
    status_msg VARCHAR(50) DEFAULT 'OFFLINE' 
); 

-- Seed Data for Step 2 
INSERT INTO nodes (node_id, node_type, parent_id, v_ratio) VALUES  
('util_a', 'UTILITY', NULL, 1.0), 
('trans_a', 'TRANSFORMER', 'util_a', 0.4); -- 120V -> 48V 
INSERT INTO live_status (node_id) VALUES ('util_a'), ('trans_a'); 