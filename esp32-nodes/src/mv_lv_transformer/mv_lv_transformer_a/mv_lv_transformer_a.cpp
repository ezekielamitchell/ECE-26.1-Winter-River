// mv_lv_transformer_a.cpp
// Chain: mv_switchgear_a → [mv_lv_transformer_a] → ats_a
// Node: mv_lv_transformer_a — 34.5 kV→480 V step-down transformer, 1000 kVA, Side A.
// States: NORMAL, WARNING, FAULT

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
const char *node_id = "mv_lv_transformer_a";

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
const int voltage_rating = 480;    // V output
const int capacity_kva   = 1000;   // kVA
int    load_pct   = 45;
float  power_kva  = 450.0;
int    temp_f     = 112;
String trf_state  = "NORMAL";      // NORMAL, WARNING, FAULT

// ─── NTP helper ──────────────────────────────────────────────────────────────
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

// ─── State guard helper ──────────────────────────────────────────────────────
void applyStateGuard() {
  if (load_pct > 90 || temp_f > 185) {
    trf_state = "FAULT";
  } else if (load_pct > 75 || temp_f > 149) {
    trf_state = "WARNING";
  }
}

// ─── MQTT callback ───────────────────────────────────────────────────────────
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (msg.startsWith("LOAD:")) {
    load_pct  = msg.substring(5).toInt();
    power_kva = (load_pct / 100.0f) * capacity_kva;

  } else if (msg.startsWith("TEMP:")) {
    temp_f = msg.substring(5).toInt();

  } else if (msg.startsWith("STATUS:")) {
    trf_state = msg.substring(7);
  }

  applyStateGuard();
}

// ─── OLED update ─────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: abbreviated label + state
  display.print("mv_trf_a [");
  display.print(trf_state);
  display.println("]");

  // Line 2: IP + RSSI
  display.print("IP:");
  display.print(WiFi.localIP().toString());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Line 3: load % and power kVA
  display.print("Load: ");
  display.print(load_pct);
  display.print("% (");
  display.print((int)power_kva);
  display.println("kVA)");

  // Line 4: temperature
  display.print("Temp: ");
  display.print(temp_f);
  display.println(" F");

  // Line 5: voltage out
  display.print(voltage_rating);
  display.println("V out");

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
  display.println("mv_lv_transformer_a");
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
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"load\":"      + String(load_pct) +
                   ",\"power_kva\":"   + String(power_kva, 1) +
                   ",\"temp_f\":"      + String(temp_f) +
                   ",\"state\":\""     + trf_state + "\"" +
                   ",\"voltage\":"     + String(voltage_rating) +
                   "}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  Serial.println(payload);

  delay(5000);
}
