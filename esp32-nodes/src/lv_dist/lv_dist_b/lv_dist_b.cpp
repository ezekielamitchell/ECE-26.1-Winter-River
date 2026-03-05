// lv_dist_b.cpp
// Chain: ats_b → [lv_dist_b] → ups_b, cooling_b, lighting_b, monitoring_b
// Node: lv_dist_b — 480 V LV distribution board, 384 kW rated, Side B.
// NOTE: Uses detectOLEDAddr() — I2C address uncertain (0x3C or 0x3D).
// States: NORMAL, OVERLOAD, FAULT, NO_INPUT

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
const char *node_id = "lv_dist_b";

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
const float RATED_VOLTAGE  = 480.0;
const float RATED_POWER_KW = 384.0;
float  input_v       = 480.0;
float  ups_load_kw   = 95.0;
float  mech_load_kw  = 42.0;
float  total_load_kw = 137.0;
int    load_pct      = 36;
String power_source  = "UTILITY";   // UTILITY, GENERATOR, NONE
String dist_state    = "NORMAL";    // NORMAL, OVERLOAD, FAULT, NO_INPUT

// ─── NTP helper ──────────────────────────────────────────────────────────────
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

// ─── OLED address detection ───────────────────────────────────────────────────
uint8_t detectOLEDAddr() {
  for (uint8_t addr : {0x3C, 0x3D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3C;
}

// ─── Load recalculation ──────────────────────────────────────────────────────
void recalcLoad() {
  total_load_kw = ups_load_kw + mech_load_kw;
  load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0f);
}

// ─── State guard helper ──────────────────────────────────────────────────────
void applyStateGuard() {
  if (input_v < 48.0f) {
    dist_state = "NO_INPUT";
  } else if (load_pct > 95) {
    dist_state = "OVERLOAD";
  } else if (load_pct > 85) {
    dist_state = "FAULT";
  } else if (dist_state != "NO_INPUT") {
    dist_state = "NORMAL";
  }
}

// ─── MQTT callback ───────────────────────────────────────────────────────────
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (msg.startsWith("INPUT:")) {
    input_v = msg.substring(6).toFloat();
    if (input_v < 48.0f) {
      dist_state   = "NO_INPUT";
      power_source = "NONE";
    } else if (input_v >= 432.0f && dist_state == "NO_INPUT") {
      dist_state = "NORMAL";
    }

  } else if (msg.startsWith("UPS:")) {
    ups_load_kw = msg.substring(4).toFloat();
    recalcLoad();

  } else if (msg.startsWith("MECH:")) {
    mech_load_kw = msg.substring(5).toFloat();
    recalcLoad();

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

  applyStateGuard();
}

// ─── OLED update ─────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: node label + state (lv_dist_b fits within 21 chars)
  display.print("lv_dist_b [");
  display.print(dist_state);
  display.println("]");

  // Line 2: IP + RSSI
  display.print("IP:");
  display.print(WiFi.localIP().toString());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Line 3: input voltage
  display.print("Vin:  ");
  display.print((int)input_v);
  display.println("V");

  // Line 4: load kW and %
  display.print("Load: ");
  display.print((int)total_load_kw);
  display.print("kW ");
  display.print(load_pct);
  display.println("%");

  // Line 5: power source
  display.print("Src: ");
  display.println(power_source);

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
  if (!display.begin(SSD1306_SWITCHCAPVCC, detectOLEDAddr())) {
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
  display.println("lv_dist_b");
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
                   "\",\"input_v\":"      + String(input_v, 1) +
                   ",\"ups_load_kw\":"    + String(ups_load_kw, 1) +
                   ",\"mech_load_kw\":"   + String(mech_load_kw, 1) +
                   ",\"total_load_kw\":"  + String(total_load_kw, 1) +
                   ",\"load_pct\":"       + String(load_pct) +
                   ",\"source\":\""       + power_source + "\"" +
                   ",\"state\":\""        + dist_state + "\"" +
                   ",\"voltage\":"        + String(RATED_VOLTAGE, 1) +
                   "}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  Serial.println(payload);

  delay(5000);
}
