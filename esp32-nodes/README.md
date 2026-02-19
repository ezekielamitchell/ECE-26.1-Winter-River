# ESP32 Nodes — MQTT with Last Will and Testament (LWT)

Guide for creating multiple ESP32 nodes with isolated MQTT topics and automatic offline detection using LWT. Reference implementation: `src/transformer/transformer_a/transformer_a.cpp`.

## Network Configuration

All nodes connect to the Raspberry Pi hotspot. Set these values at the top of each node's `.cpp` file:

| Constant | Value |
|----------|-------|
| `ssid` | `WinterRiver-AP` |
| `password` | `winterriver` |
| `mqtt_server` | `192.168.4.1` |

The Pi must be running its hotspot (`sudo ./scripts/setup_hotspot.sh`) and Mosquitto before flashing nodes.

> **2.4 GHz required:** The ESP32 is a 2.4 GHz-only chip. The hotspot script forces `wifi.band bg` on channel 6 so nodes can always find the network. If nodes show zero signal bars, confirm the hotspot is on 2.4 GHz with `sudo ./scripts/setup_hotspot.sh status`.

---

## Topic Structure

Each node uses three dedicated topics under a shared prefix:

```
winter-river/<node_id>/status    # Node publishes telemetry (JSON)
winter-river/<node_id>/control   # Node subscribes for commands
winter-river/<node_id>/lwt       # Broker publishes if node disconnects
```

Examples for multiple nodes:

| Node         | `node_id` | Status Topic                      | Control Topic                      |
|--------------|-----------|-----------------------------------|------------------------------------|
| Transformer A| `trf_a`   | `winter-river/trf_a/status`       | `winter-river/trf_a/control`       |
| Transformer B| `trf_b`   | `winter-river/trf_b/status`       | `winter-river/trf_b/control`       |
| PDU A        | `pdu_a`   | `winter-river/pdu_a/status`       | `winter-river/pdu_a/control`       |
| Server Rack 1| `srv_1`   | `winter-river/srv_1/status`       | `winter-river/srv_1/control`       |

## How LWT Works

LWT is a message you register with the broker **at connect time**. If the node drops unexpectedly (power loss, crash, network failure), the broker automatically publishes that message on the node's behalf.

```
1. Node connects → tells broker: "if I vanish, publish OFFLINE to my status topic"
2. Node publishes ONLINE (retained) → overrides any previous OFFLINE message
3. Node crashes → broker publishes the OFFLINE message (retained)
4. Any subscriber or new client sees OFFLINE immediately
```

The `retained` flag is key — it means the last message persists on the topic so new subscribers always see the current state.

## Implementation Steps

### 1. Set the Node Identity

Change `node_id` per node. This single constant drives all topic routing:

```cpp
const char *node_id = "trf_a";  // unique per node: trf_b, pdu_a, srv_1, etc.
```

### 2. Build Topic Strings

Derive all topics from `node_id`:

```cpp
String base          = String("winter-river/") + node_id;
String status_topic  = base + "/status";
String control_topic = base + "/control";
String lwt_topic     = base + "/status";  // LWT publishes to the same status topic
```

### 3. Connect with LWT

Pass the LWT message to `mqtt.connect()`. Parameters: `clientId, lwtTopic, lwtQoS, lwtRetain, lwtMessage`.

```cpp
String lwt_message = String("{\"node\":\"") + node_id + "\",\"status\":\"OFFLINE\"}";

if (mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_message.c_str())) {
    // QoS 1 = at least once delivery
    // retained = true → persists as last known state
```

### 4. Publish ONLINE on Successful Connect

Immediately after connecting, publish a retained ONLINE message to override any stale OFFLINE:

```cpp
String online_msg = String("{\"ts\":\"") + getTimestamp() +
                    "\",\"node\":\"" + node_id +
                    "\",\"status\":\"ONLINE\"}";
mqtt.publish(status_topic.c_str(), online_msg.c_str(), true);  // retained
```

### 5. Subscribe to Control Topic

Each node listens only on its own control topic:

```cpp
mqtt.subscribe(control_topic.c_str());
```

### 6. Handle Incoming Commands

Parse commands in the MQTT callback:

```cpp
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (message.startsWith("LOAD:")) {
        load_percent = message.substring(5).toInt();
    } else if (message.startsWith("TEMP:")) {
        temp_f = message.substring(5).toInt();
    } else if (message.startsWith("STATUS:")) {
        status_str = message.substring(7);
    }
}
```

### 7. Publish Telemetry in `loop()`

```cpp
String payload = String("{\"ts\":\"") + getTimestamp() +
                 "\",\"load\":" + load_percent +
                 ",\"power_kva\":" + power_kva +
                 ",\"temp_f\":" + temp_f +
                 ",\"status\":\"" + status_str +
                 "\",\"voltage\":" + voltage_rating + "}";
mqtt.publish(status_topic.c_str(), payload.c_str());
```

## Creating a New Node

1. Copy an existing node folder (e.g. `src/transformer/transformer_a/`) to a new path like `src/transformer/transformer_b/`.
2. Change `node_id`, component-specific constants (voltage, capacity, etc.), and filenames.
3. Add a PlatformIO env in `platformio.ini`:
   ```ini
   [env:transformer_b]
   build_src_filter = +<transformer/transformer_b/>
   ```
4. Build and upload:
   ```bash
   cd esp32-nodes
   pio run -e transformer_b -t upload
   ```

## Testing with `mosquitto_sub`

Monitor all nodes at once:

```bash
mosquitto_sub -h <broker_ip> -t "winter-river/#" -v
```

Monitor a single node:

```bash
mosquitto_sub -h <broker_ip> -t "winter-river/trf_a/#" -v
```

Send a command to a node:

```bash
mosquitto_pub -h <broker_ip> -t "winter-river/trf_a/control" -m "LOAD:80"
```

## PlatformIO Build

All commands run from the `esp32-nodes/` directory:

```bash
pio run -e transformer_a           # compile
pio run -e transformer_a -t upload # flash
pio device monitor                 # serial output
```

Available environments: `transformer_a`, `pdu_a`, `pdu_b`, `server`.

### Libraries (`platformio.ini`)

| Library | Used by |
|---------|---------|
| `knolleary/PubSubClient@^2.8` | All nodes — MQTT client |
| `adafruit/Adafruit SSD1306@^2.5.7` | `transformer_a` — 128×64 OLED |
| `adafruit/Adafruit GFX Library@^1.11.5` | `transformer_a` — OLED graphics |
| `marcoschwartz/LiquidCrystal_I2C@^1.1.4` | `pdu_a`, `pdu_b`, `server` — 16×2 LCD |

All libraries are declared in the shared `[env]` block so every environment has access to them.
