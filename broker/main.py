import paho.mqtt.client as mqtt
import psycopg2
from psycopg2.extras import RealDictCursor
import json
import time
import toml
import os

# --- CONFIGURATION ---
# Load from config.toml (copy from config.sample.toml, never commit)
_cfg_path = os.path.join(os.path.dirname(__file__), "config.toml")
_cfg = toml.load(_cfg_path)

MQTT_BROKER = _cfg["mqtt"]["broker_host"]       # 192.168.4.1 on Pi
MQTT_PORT   = _cfg["mqtt"]["broker_port"]        # 1883
DB_CONFIG   = _cfg["database"]["dsn"]            # postgres DSN string

class WinterRiverEngine:
    def __init__(self):
        self.db = psycopg2.connect(DB_CONFIG, cursor_factory=RealDictCursor)
        self.mqtt = mqtt.Client()
        self.mqtt.on_message = self.on_message
        self.mqtt.connect(MQTT_BROKER, MQTT_PORT)
        self.mqtt.subscribe("winter-river/+/status")
        self.mqtt.loop_start()

    def on_message(self, client, userdata, msg):
        """Processes incoming data from ESP32 nodes"""
        try:
            node_id = msg.topic.split('/')[1]
            try:
                payload = json.loads(msg.payload)
            except json.JSONDecodeError:
                # pdu_a publishes a plain string, not JSON — treat as present
                payload = {"status": "ONLINE"}
            
            # Detect removal via LWT or status message
            is_present = (payload.get('status') != 'OFFLINE')
            
            with self.db.cursor() as cur:
                # Update database with latest telemetry
                cur.execute("UPDATE live_status SET is_present = %s, last_update = NOW() WHERE node_id = %s", 
                           (is_present, node_id))
                cur.execute("INSERT INTO historical_data (node_id, metrics) VALUES (%s, %s)",
                           (node_id, json.dumps(payload)))
                self.db.commit()
        except Exception as e:
            print(f"Error processing message: {e}")

    def run_simulation_tick(self):
        """Calculates value propagation through the hierarchy"""
        with self.db.cursor() as cur:
            # 1. Fetch current state
            cur.execute("SELECT n.*, l.* FROM nodes n JOIN live_status l ON n.node_id = l.node_id")
            nodes = {row['node_id']: row for row in cur.fetchall()}

            # 2. Logic: Top-Down Propagation
            for nid, node in nodes.items():
                v_out = 0.0
                state = "NORMAL"

                if node['node_type'] == 'UTILITY':
                    v_out = 230.0 if node['is_present'] else 0.0  # kV
                
                elif node['node_type'] == 'GENERATOR':
                    # Trigger start sequence if Utility is gone
                    side = node['side']
                    util_node = nodes[f'util_{side.lower()}']
                    if not util_node['is_present'] and node['is_present']:
                        if node['gen_timer'] > 0:
                            node['gen_timer'] -= 1
                            state = "STARTING"
                        else:
                            v_out = 480.0
                            state = "RUNNING"
                    else:
                        node['gen_timer'] = 10 # Reset timer
                        state = "STANDBY"

                elif node['node_type'] == 'UPS':
                    parent = nodes.get(node['parent_id'])
                    if parent and parent['v_out'] > 0:
                        v_out = 480.0
                        state = "CHARGING"
                        node['battery_level'] = min(100, node['battery_level'] + 1)
                    elif node['battery_level'] > 0:
                        v_out = 480.0
                        state = "ON_BATTERY"
                        node['battery_level'] -= 1 # Drain battery
                    else:
                        state = "FAULT"

                else: # TRANSFORMER, SW_GEAR, DIST_BOARD, PDU, SERVER_RACK
                    parent = nodes.get(node['parent_id'])
                    if node['node_type'] == 'SW_GEAR':
                        # ATS: passes whichever source is live — utility path or generator
                        side = node['side'].lower()
                        gen = nodes[f'gen_{side}']
                        util = nodes[f'util_{side}']
                        util_v = parent['v_out'] if parent else 0.0
                        gen_v = gen['v_out']
                        v_out = max(util_v, gen_v)
                    elif node['node_type'] == 'DIST_BOARD':
                        side = node['side'].lower()
                        gen = nodes[f'gen_{side}']
                        util = nodes[f'util_{side}']
                        v_out = (parent['v_out'] * node['v_ratio']) if parent else 0.0
                        if util['is_present'] and util['v_out'] > 0:
                            node['_source'] = 'UTILITY'
                        elif gen['v_out'] > 0:
                            node['_source'] = 'GENERATOR'
                        else:
                            node['_source'] = 'NONE'
                        if v_out == 0.0:
                            state = 'NO_INPUT'
                    else:
                        v_out = (parent['v_out'] * node['v_ratio']) if parent else 0.0
                
                # 3. Update DB and Broadcast to Physical ESP32
                node['v_out'] = v_out
                node['status_msg'] = state
                self.update_node(node)

    def update_node(self, node):
        """Sends command back to ESP32 and updates DB"""
        ntype = node['node_type']
        status = node['status_msg']
        v = node['v_out']

        # Build control command matching firmware mqttCallback per node type
        if ntype == 'UTILITY':
            cmd = f"VOLT:{v} STATUS:{status}"
        elif ntype == 'TRANSFORMER':
            cmd = f"STATUS:{status}"
        elif ntype == 'SW_GEAR':
            cmd = f"CLOSE STATUS:{status}" if v > 0 else f"OPEN STATUS:{status}"
        elif ntype == 'GENERATOR':
            rpm = 1800 if v > 0 else 0
            cmd = f"RPM:{rpm} STATUS:{status}"
        elif ntype == 'DIST_BOARD':
            cmd = f"INPUT:{v} SOURCE:{node.get('_source', 'NONE')} STATUS:{status}"
        elif ntype == 'UPS':
            cmd = f"INPUT:{v} BATT:{node['battery_level']} STATUS:{status}"
        elif ntype == 'PDU':
            cmd = f"STATUS:{status}"
        elif ntype == 'SERVER_RACK':
            cmd = f"STATUS:{status}"
        else:
            cmd = f"STATUS:{status}"

        self.mqtt.publish(f"winter-river/{node['node_id']}/control", cmd)
        
        with self.db.cursor() as cur:
            cur.execute("""UPDATE live_status SET v_out = %s, status_msg = %s,
                        battery_level = %s, gen_timer = %s WHERE node_id = %s""",
                        (node['v_out'], node['status_msg'], node['battery_level'],
                         node['gen_timer'], node['node_id']))
            self.db.commit()

if __name__ == "__main__":
    engine = WinterRiverEngine()
    print("Winter River Simulation Engine Running...")
    while True:
        engine.run_simulation_tick()
        time.sleep(1) # 1 second simulation tick