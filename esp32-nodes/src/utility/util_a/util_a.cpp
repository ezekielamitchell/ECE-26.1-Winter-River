#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ─── NODE IDENTITY ────────────────────────────────────────────────────────────
// util_a: MV Utility Feed — Side A
//
// Role:    Grid source. Root of the entire Side-A power hierarchy.
//          No parent node — this IS the supply. Its disconnect triggers the
//          full cascade failure scenario (Step 2 / Step 3 demo).
//
// Position in chain:
//   [util_a] → trf_a → sw_a → gen_a → dist_a → ups_a → pdu_a → srv_a
//
// Electrical parameters (real-world MV utility):
//   Vin  : N/A (infinite bus / grid)
//   Vout : 230,000 V  (230 kV MV feed to site)
//   Freq : 60 Hz
//   Phase: 3-phase
//   Rated: ~50 MVA service capacity (simplified for simulation)
//
// MQTT LWT: if this node disconnects, broker publishes OFFLINE → cascades
//           downstream nodes to v_out = 0 in the simulation engine.
// ─────────────────────────────────────────────────────────────────────────────

const char *node_id = "util_a";

// Grid parameters — 230 kV MV feed
const float GRID_VOLTAGE_KV  = 230.0;   // kV output to site HV yard
const float FREQ_HZ          = 60.0;    // grid frequency (Hz)
const int   PHASE_COUNT      = 3;       // 3-phase supply

// ── Simulated metrics (can be overridden via MQTT control commands) ───────────
float  grid_voltage_kv  = 230.0;   // actual output voltage (kV)
float  freq_hz          = 60.0;    // actual frequency (Hz)
int    load_pct         = 12;      // site load as % of utility capacity
String grid_state       = "GRID_OK"; // GRID_OK, SAG, SWELL, OUTAGE, FAULT

// ── Network ───────────────────────────────────────────────────────────────────
const char *ssid        = "WinterRiver-AP";
const char *password    = "winterriver";
const char *mqtt_server = "192.168.4.1";

// ── NTP ───────────────────────────────────────────────────────────────────────
const char* ntp_server          = "192.168.4.1";
const long  gmt_offset_sec      = -28800;  // PST = UTC-8
const int   daylight_offset_sec = 3600;

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ── LCD — pointer, allocated in setup() after I2C address scan ───────────────
LiquidCrystal_I2C *lcd = nullptr;

uint8_t detectLCDAddr() {
  for (uint8_t addr : {0x27, 0x3F}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3F;
}

int message_count = 0;

// ── MQTT callback — accepts control commands ──────────────────────────────────
// Commands:
//   STATUS:GRID_OK   → normal operation
//   STATUS:SAG       → voltage sag (< 10% below nominal)
//   STATUS:SWELL     → voltage swell (> 10% above nominal)
//   STATUS:OUTAGE    → full loss of utility (cascades Side A offline)
//   VOLT:xxx.x       → override output voltage (kV)
//   FREQ:xx.x        → override frequency
//   LOAD:xx          → set site load percentage
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("STATUS:")) {
    grid_state = msg.substring(7);
    if      (grid_state == "OUTAGE") grid_voltage_kv = 0.0;
    else if (grid_state == "SAG")    grid_voltage_kv = GRID_VOLTAGE_KV * 0.88;
    else if (grid_state == "SWELL")  grid_voltage_kv = GRID_VOLTAGE_KV * 1.10;
    else                             grid_voltage_kv = GRID_VOLTAGE_KV;
  } else if (msg.startsWith("VOLT:")) {
    grid_voltage_kv = msg.substring(5).toFloat();
    if      (grid_voltage_kv == 0.0)                    grid_state = "OUTAGE";
    else if (grid_voltage_kv < GRID_VOLTAGE_KV * 0.90)  grid_state = "SAG";
    else if (grid_voltage_kv > GRID_VOLTAGE_KV * 1.10)  grid_state = "SWELL";
    else                                                 grid_state = "GRID_OK";
  } else if (msg.startsWith("FREQ:")) {
    freq_hz = msg.substring(5).toFloat();
    if (freq_hz < 59.3 || freq_hz > 60.7) grid_state = "FAULT";
  } else if (msg.startsWith("LOAD:")) {
    load_pct = msg.substring(5).toInt();
  }
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // LCD first — before WiFi radio to avoid I2C interference
  Wire.begin();
  lcd = new LiquidCrystal_I2C(detectLCDAddr(), 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->setCursor(0, 0);
  lcd->print("Connecting...");

  // WiFi — full radio reset
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(200);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  WiFi.begin(ssid, password);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_start > 20000) {
      int s = WiFi.status();
      Serial.println("\nWiFi failed (status=" + String(s) + ") — waiting 30 s for hotspot then restarting");
      lcd->clear(); lcd->setCursor(0, 0); lcd->print("WiFi FAILED s="); lcd->print(s);
      lcd->setCursor(0, 1); lcd->print("Wait 30s...");
      delay(30000);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }

  lcd->clear(); lcd->setCursor(0, 0);
  lcd->print(node_id); lcd->print(" OK");
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  // NTP sync
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries++ < 10) delay(500);
  Serial.println(retries >= 10 ? "\nNTP failed" : "\nTime: " + getTimestamp());

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);
}

// ── loop ──────────────────────────────────────────────────────────────────────
void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    String lwt_topic = String("winter-river/") + node_id + "/status";
    String lwt_msg   = String("{\"node\":\"") + node_id + "\",\"status\":\"OFFLINE\"}";
    if (mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_msg.c_str())) {
      Serial.println("connected");
      String online = String("{\"ts\":\"") + getTimestamp() +
                      "\",\"node\":\"" + node_id +
                      "\",\"status\":\"ONLINE\"}";
      mqtt.publish(lwt_topic.c_str(), online.c_str(), true);
      String ctrl = String("winter-river/") + node_id + "/control";
      mqtt.subscribe(ctrl.c_str());
      Serial.println("Subscribed to: " + ctrl);
    } else {
      Serial.println("failed, state=" + String(mqtt.state()));
      lcd->clear(); lcd->setCursor(0, 0); lcd->print("MQTT FAIL");
      lcd->setCursor(0, 1); lcd->print("st="); lcd->print(mqtt.state());
      delay(2000);
      return;
    }
  }

  // ── LCD display ──────────────────────────────────────────────────────────────
  // Line 0: "util_a GRID_OK"
  // Line 1: "230kV 60Hz 12%"
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(node_id);
  lcd->print(" ");
  lcd->print(grid_state.substring(0, 8));

  lcd->setCursor(0, 1);
  lcd->print((int)grid_voltage_kv);
  lcd->print("kV ");
  lcd->print((int)freq_hz);
  lcd->print("Hz ");
  lcd->print(load_pct);
  lcd->print("%");

  mqtt.loop();

  // ── Publish telemetry ─────────────────────────────────────────────────────────
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"")        + getTimestamp()  +
                   "\",\"v_out\":"             + grid_voltage_kv +
                   ",\"freq_hz\":"             + freq_hz         +
                   ",\"load_pct\":"            + load_pct        +
                   ",\"state\":\""             + grid_state      +
                   "\",\"voltage_kv\":"        + GRID_VOLTAGE_KV +
                   ",\"phase\":"               + PHASE_COUNT     + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;

  delay(5000);
}
