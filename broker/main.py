# TODO: Implement broker management utilities using paho-mqtt
import paho.mqtt.client as mqtt
import psycopg2
from psycopg2.extras import RealDictCursor
import json
import time

# --- CONFIGURATION ---
MQTT_BROKER = "localhost" # or Pi IP
DB_CONFIG = "host=localhost dbname=winter_river user=postgres password=password"

class WinterRiverEngine:
    def __init__(self):
        self.db = psycopg2.connect(DB_CONFIG, cursor_factory=RealDictCursor)
        self.mqtt = mqtt.Client()
        self.mqtt.on_message = self.on_message
        self.mqtt.connect(MQTT_BROKER, 1883)
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
                    v_out = 230000 if node['is_present'] else 0
                
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
                        state = "BLACKOUT"

                else: # Transformers, SW_GEAR, PDU, RACK
                    parent = nodes.get(node['parent_id'])
                    # Special Logic for SW_GEAR: Check Utility OR Generator
                    if node['node_type'] == 'SW_GEAR':
                        gen = nodes[f'gen_{node["side"].lower()}']
                        source_v = max(parent['v_out'] if parent else 0, gen['v_out'])
                        v_out = source_v
                    else:
                        v_out = (parent['v_out'] * node['v_ratio']) if parent else 0
                
                # 3. Update DB and Broadcast to Physical ESP32
                node['v_out'] = v_out
                node['status_msg'] = state
                self.update_node(node)

    def update_node(self, node):
        """Sends command back to ESP32 and updates DB"""
        # Publish to control topic (e.g., winter-river/ups_a/control)
        cmd = f"INPUT:{node['v_out']} STATUS:{node['status_msg']}"
        if node['node_type'] == 'UPS': cmd += f" BATT:{node['battery_level']}"
        if node['node_type'] == 'GENERATOR': cmd += f" RPM:{1800 if node['v_out'] > 0 else 0}"
        
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