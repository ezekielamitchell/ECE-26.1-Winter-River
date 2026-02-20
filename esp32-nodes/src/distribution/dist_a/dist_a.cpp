#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ─── NODE IDENTITY ────────────────────────────────────────────────────────────
// dist_a: Main LV Distribution Board — Side A
//
// Role:    480 V distribution hub. Receives power from the Main LV Switchgear
//          (sw_a) and distributes it to the UPS (ups_a) and mechanical loads
//          (HVAC, lighting, etc.).  The ATS decision (utility vs. generator)
//          has already been made upstream in sw_a; dist_a simply passes through
//          whichever 480 V source the ATS selected.
//
// Position in chain:
//   util_a → trf_a → sw_a → [dist_a] → ups_a → pdu_a → srv_a
//                    gen_a ↗
//
// Electrical parameters:
//   Vin  : 480 V  (3-phase, from sw_a / ATS output)
//   Vout : 480 V  (pass-through to UPS + mechanical loads)
//   Rated: 800 A  (384 kVA @ 480 V, 3-phase)
//   Breakers: UPS feed + mechanical (HVAC / lighting) branches
//
// Metrics tracked:
//   input_v       — measured 480 V bus voltage
//   ups_load_kw   — power delivered to UPS branch
//   mech_load_kw  — power delivered to mechanical branch (HVAC etc.)
//   total_load_kw — sum of all branch loads
//   load_pct      — total as % of 384 kW rated capacity
//   power_source  — which upstream source is active: UTILITY | GENERATOR | NONE
//   dist_state    — NORMAL | OVERLOAD | FAULT | NO_INPUT
// ─────────────────────────────────────────────────────────────────────────────

const char *node_id = "dist_a";

// Board ratings — 480 V, 3-phase, 800 A main breaker
const float RATED_VOLTAGE    = 480.0;   // V
const float RATED_CURRENT_A  = 800.0;   // A (main breaker)
const float RATED_POWER_KW   = 384.0;   // kW  (480 V × 800 A × √3 ≈ 665 kVA; ~384 kW at 0.9 PF)

// ── Simulated metrics (can be overridden via MQTT control commands) ───────────
float  input_v        = 480.0;   // measured bus voltage (V)
float  ups_load_kw    = 95.0;    // UPS branch load (kW)   — ~25% of rated
float  mech_load_kw   = 42.0;    // mechanical branch (HVAC/lighting) load (kW)
float  total_load_kw  = 137.0;   // total distribution load (kW)
int    load_pct       = 36;      // total load as % of rated
String power_source   = "UTILITY"; // UTILITY | GENERATOR | NONE
String dist_state     = "NORMAL";  // NORMAL | OVERLOAD | FAULT | NO_INPUT

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

// ── LCD (16x2 I2C — try 0x27 if 0x3F shows no text) ─────────────────────────
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

// ── MQTT callback — accepts control commands ──────────────────────────────────
// Commands:
//   INPUT:xxx.x     → set bus voltage (V) — set to 0 to simulate upstream loss
//   UPS:xxx.x       → set UPS branch load (kW)
//   MECH:xxx.x      → set mechanical branch load (kW)
//   SOURCE:UTILITY  → mark active source as utility feed
//   SOURCE:GENERATOR→ mark active source as generator feed
//   SOURCE:NONE     → mark as de-energised (both sources lost)
//   STATUS:NORMAL / OVERLOAD / FAULT / NO_INPUT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("INPUT:")) {
    input_v = msg.substring(6).toFloat();
    // If bus drops below 10% of nominal → NO_INPUT
    if (input_v < RATED_VOLTAGE * 0.10) {
      dist_state   = "NO_INPUT";
      power_source = "NONE";
    } else if (input_v >= RATED_VOLTAGE * 0.90) {
      // Voltage restored — keep current source label, reset state if was NO_INPUT
      if (dist_state == "NO_INPUT") dist_state = "NORMAL";
    }
  } else if (msg.startsWith("UPS:")) {
    ups_load_kw  = msg.substring(4).toFloat();
    total_load_kw = ups_load_kw + mech_load_kw;
    load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0);
  } else if (msg.startsWith("MECH:")) {
    mech_load_kw  = msg.substring(5).toFloat();
    total_load_kw = ups_load_kw + mech_load_kw;
    load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0);
  } else if (msg.startsWith("SOURCE:")) {
    power_source = msg.substring(7);
    if (power_source == "NONE") {
      dist_state = "NO_INPUT";
      input_v    = 0.0;
    } else if (dist_state == "NO_INPUT") {
      dist_state = "NORMAL";
      input_v    = RATED_VOLTAGE;
    }
  } else if (msg.startsWith("STATUS:")) {
    dist_state = msg.substring(7);
  }

  // Auto-thresholds
  if (input_v < RATED_VOLTAGE * 0.10) {
    dist_state = "NO_INPUT";
  } else if (load_pct > 95) {
    dist_state = "OVERLOAD";
  } else if (load_pct > 85) {
    dist_state = "FAULT";   // approaching overload — warn before trip
  } else if (dist_state != "NO_INPUT") {
    dist_state = "NORMAL";
  }
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // LCD first — before WiFi radio to avoid I2C interference
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

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
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("WiFi FAILED s="); lcd.print(s);
      lcd.setCursor(0, 1); lcd.print("Wait 30s...");
      delay(30000);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }

  lcd.clear(); lcd.setCursor(0, 0);
  lcd.print(node_id); lcd.print(" OK");
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
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("MQTT FAIL");
      lcd.setCursor(0, 1); lcd.print("st="); lcd.print(mqtt.state());
      delay(2000);
      return;
    }
  }

  // ── LCD display ──────────────────────────────────────────────────────────────
  // Line 0: "dist_a NORMAL"  (or OVERLOAD / FAULT / NO_INP)
  // Line 1: "480V 137kW 36%"
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(node_id);
  lcd.print(" ");
  // Shorten NO_INPUT to NO_INP so it fits
  String state_short = dist_state.substring(0, 8);
  if (dist_state == "NO_INPUT") state_short = "NO_INP";
  lcd.print(state_short);

  lcd.setCursor(0, 1);
  lcd.print((int)input_v);
  lcd.print("V ");
  lcd.print((int)total_load_kw);
  lcd.print("kW ");
  lcd.print(load_pct);
  lcd.print("%");

  mqtt.loop();

  // ── Publish telemetry ─────────────────────────────────────────────────────────
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"")       + getTimestamp()  +
                   "\",\"input_v\":"          + input_v         +
                   ",\"ups_load_kw\":"        + ups_load_kw     +
                   ",\"mech_load_kw\":"       + mech_load_kw    +
                   ",\"total_load_kw\":"      + total_load_kw   +
                   ",\"load_pct\":"           + load_pct        +
                   ",\"source\":\""           + power_source    +
                   "\",\"state\":\""          + dist_state      +
                   "\",\"voltage\":"          + RATED_VOLTAGE   + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;

  delay(5000); // 5 s publish interval
}
