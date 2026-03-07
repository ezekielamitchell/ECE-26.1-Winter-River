// utility_b.cpp
// Chain: utility_b → mv_switchgear_b → mv_lv_transformer_b / generator_b → ats_b → lv_dist_b → ups_b → pdu_b → rectifier_b → server_rack
// Node: utility_b — Root node. 230 kV 3-phase grid source, Side B.
// States: GRID_OK, SAG, SWELL, OUTAGE, FAULT
// LWT disconnect triggers full Side-B cascade.

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ─── Network constants ───────────────────────────────────────────────────────
const char *ssid        = "WinterRiver-AP";
const char *password    = "winterriver";
const char *mqtt_server = "192.168.4.1";
const char *ntp_server          = "192.168.4.1";
const long  gmt_offset_sec      = -28800;
const int   daylight_offset_sec = 3600;

// ─── Node identity ───────────────────────────────────────────────────────────
const char *node_id = "utility_b";

// ─── MQTT / WiFi clients (declared before OLED) ──────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ─── OLED ────────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

// ─── State variables ─────────────────────────────────────────────────────────
float  grid_voltage_kv    = 230.0;
float  freq_hz            = 60.0;
int    load_pct           = 12;
String grid_state         = "GRID_OK";  // GRID_OK, SAG, SWELL, OUTAGE, FAULT

const float GRID_VOLTAGE_KV = 230.0;
const float FREQ_HZ         = 60.0;
const int   PHASE_COUNT     = 3;

// ─── NTP helper ──────────────────────────────────────────────────────────────
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

// ─── MQTT callback ───────────────────────────────────────────────────────────
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  // Parse space-separated tokens so compound commands work.
  // e.g. "TOKEN1:val1 TOKEN2:val2" sets both fields.
  int start = 0;
  while (start <= (int)msg.length()) {
    int sp = msg.indexOf(' ', start);
    String tok = (sp < 0) ? msg.substring(start) : msg.substring(start, sp);

    if (tok.startsWith("STATUS:")) {
      grid_state = tok.substring(7);
      if      (grid_state == "OUTAGE") grid_voltage_kv = 0.0;
      else if (grid_state == "SAG")    grid_voltage_kv = GRID_VOLTAGE_KV * 0.88f;
      else if (grid_state == "SWELL")  grid_voltage_kv = GRID_VOLTAGE_KV * 1.10f;
      else                             grid_voltage_kv = GRID_VOLTAGE_KV;

    } else if (tok.startsWith("VOLT:")) {
      grid_voltage_kv = tok.substring(5).toFloat();
      float ratio = grid_voltage_kv / GRID_VOLTAGE_KV;
      if      (grid_voltage_kv <= 0.0f) grid_state = "OUTAGE";
      else if (ratio < 0.90f)           grid_state = "SAG";
      else if (ratio > 1.10f)           grid_state = "SWELL";
      else                              grid_state = "GRID_OK";

    } else if (tok.startsWith("FREQ:")) {
      freq_hz = tok.substring(5).toFloat();
      if (freq_hz < 59.3f || freq_hz > 60.7f) grid_state = "FAULT";

    } else if (tok.startsWith("LOAD:")) {
      load_pct = tok.substring(5).toInt();
    }

    if (sp < 0) break;
    start = sp + 1;
  }
}

// ─── OLED update ─────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: node label + state
  display.print("utility_b [");
  display.print(grid_state);
  display.println("]");

  // Line 2: IP + RSSI
  display.print("IP:");
  display.print(WiFi.localIP().toString());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Line 3: voltage out
  display.print("Vout: ");
  display.print(grid_voltage_kv, 0);
  display.println("kV");

  // Line 4: frequency
  display.print("Freq: ");
  display.print(freq_hz, 2);
  display.println("Hz");

  // Line 5: load
  display.print("Load: ");
  display.print(load_pct);
  display.println("%");

  // Line 6: MQTT status + message count
  display.print("MQTT:");
  display.print(mqtt.connected() ? "OK" : "ERR");
  display.print(" Msgs:");
  display.println(message_count);

  display.display();
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // OLED first — before WiFi to avoid I2C interference
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting...");
  display.display();

  // Full WiFi reset sequence
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(200);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  WiFi.begin(ssid, password);

  // 20-second WiFi timeout
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi FAILED");
    display.display();
    Serial.println("WiFi failed — restarting in 30s");
    delay(30000);
    ESP.restart();
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("utility_b");
  display.println("WiFi OK");
  display.display();
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());

  // NTP sync
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
  struct tm timeinfo;
  int ntpRetry = 0;
  while (!getLocalTime(&timeinfo) && ntpRetry < 10) {
    delay(500);
    ntpRetry++;
  }

  // MQTT setup
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void loop() {
  // MQTT reconnect
  if (!mqtt.connected()) {
    String lwt_topic = String("winter-river/") + node_id + "/status";
    String lwt_msg   = String("{\"node\":\"") + node_id + "\",\"status\":\"OFFLINE\"}";
    if (mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_msg.c_str())) {
      String online = String("{\"ts\":\"") + getTimestamp() + "\",\"node\":\"" + node_id + "\",\"status\":\"ONLINE\"}";
      mqtt.publish(lwt_topic.c_str(), online.c_str(), true);
      String ctrl = String("winter-river/") + node_id + "/control";
      mqtt.subscribe(ctrl.c_str());
      Serial.println("MQTT connected");
    } else {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("MQTT FAILED");
      display.display();
      delay(2000);
      return;
    }
  }

  // Update display and increment message count
  message_count++;
  updateDisplay();

  // Process incoming MQTT messages
  mqtt.loop();

  // Build and publish telemetry JSON
  String topic = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"v_out\":"     + String(grid_voltage_kv, 1) +
                   ",\"freq_hz\":"     + String(freq_hz, 1) +
                   ",\"load_pct\":"    + String(load_pct) +
                   ",\"state\":\""     + grid_state + "\"" +
                   ",\"voltage_kv\":"  + String(grid_voltage_kv, 1) +
                   ",\"phase\":"       + String(PHASE_COUNT) +
                   "}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  Serial.println(payload);

  delay(5000);
}
