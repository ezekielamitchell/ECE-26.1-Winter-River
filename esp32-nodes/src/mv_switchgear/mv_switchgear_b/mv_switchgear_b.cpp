// mv_switchgear_b.cpp
// Chain: utility_b → [mv_switchgear_b] → mv_lv_transformer_b
// Node: mv_switchgear_b — 34.5 kV medium-voltage switchgear, Side B.
// States: CLOSED, OPEN, TRIPPED, FAULT

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
const char *node_id = "mv_switchgear_b";

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
const int voltage_rating = 34500;   // volts (34.5 kV)
bool   breaker_closed    = true;
float  current_a         = 15.2;
float  load_kw           = 420.0;
int    load_pct          = 30;
String sw_state          = "CLOSED"; // CLOSED, OPEN, TRIPPED, FAULT

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

  if (msg == "CLOSE") {
    breaker_closed = true;
    sw_state       = "CLOSED";

  } else if (msg == "OPEN") {
    breaker_closed = false;
    sw_state       = "OPEN";

  } else if (msg.startsWith("LOAD:")) {
    load_pct  = msg.substring(5).toInt();
    load_kw   = load_pct * 14.0f;
    current_a = (load_kw * 1000.0f) / voltage_rating;

  } else if (msg.startsWith("STATUS:")) {
    sw_state = msg.substring(7);
  }

  // Protection guards
  if (current_a > 40.0f || load_pct > 95) {
    sw_state       = "TRIPPED";
    breaker_closed = false;
  } else if (current_a > 32.0f || load_pct > 80) {
    sw_state = "FAULT";
  }
}

// ─── OLED update ─────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: abbreviated label + state (fits within 21 chars at text size 1)
  display.print("mv_sw_b [");
  display.print(sw_state);
  display.println("]");

  // Line 2: IP + RSSI
  display.print("IP:");
  display.print(WiFi.localIP().toString());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Line 3: current
  display.print("Current: ");
  display.print((int)current_a);
  display.println("A");

  // Line 4: load %
  display.print("Load:    ");
  display.print(load_pct);
  display.println("%");

  // Line 5: voltage out
  display.print("Vout: ");
  display.print(voltage_rating);
  display.println("V");

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
  display.println("mv_switchgear_b");
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
                   "\",\"breaker\":"   + String(breaker_closed ? "true" : "false") +
                   ",\"current_a\":"   + String(current_a, 1) +
                   ",\"load_kw\":"     + String(load_kw, 1) +
                   ",\"load_pct\":"    + String(load_pct) +
                   ",\"state\":\""     + sw_state + "\"" +
                   ",\"voltage\":"     + String(voltage_rating) +
                   "}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  Serial.println(payload);

  delay(5000);
}
