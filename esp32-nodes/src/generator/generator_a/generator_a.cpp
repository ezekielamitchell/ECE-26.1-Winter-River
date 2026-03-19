// generator_a.cpp
// Chain: [generator_a] → ats_a (backup path)
// Node: generator_a — 480 V diesel generator backup, Side A.
// States: STANDBY, STARTING, RUNNING, FAULT
// NOTE: Uses detectOLEDAddr() — SA0 pin may be HIGH, address uncertain (0x3C or 0x3D).

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
const char *node_id = "generator_a";

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
const int voltage_rating = 480;
int    fuel_pct   = 85;
int    rpm        = 0;
float  output_v   = 0.0;
int    load_pct   = 0;
String run_state  = "STANDBY";  // STANDBY, STARTING, RUNNING, FAULT

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

// ─── RPM → state derivation ──────────────────────────────────────────────────
void deriveStateFromRPM() {
  if (rpm > 1500) {
    run_state = "RUNNING";
    output_v  = voltage_rating;
  } else if (rpm > 0) {
    run_state = "STARTING";
    output_v  = 0.0;
  } else {
    run_state = "STANDBY";
    output_v  = 0.0;
  }
}

// ─── Protection guard ────────────────────────────────────────────────────────
void applyFaultGuard() {
  if (fuel_pct < 5 || (run_state == "RUNNING" && rpm < 800)) {
    run_state = "FAULT";
  }
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

    if (tok.startsWith("FUEL:")) {
      fuel_pct = tok.substring(5).toInt();

    } else if (tok.startsWith("RPM:")) {
      rpm = tok.substring(4).toInt();
      output_v = (rpm > 0) ? voltage_rating : 0.0;
      deriveStateFromRPM();

    } else if (tok.startsWith("LOAD:")) {
      load_pct = tok.substring(5).toInt();

    } else if (tok.startsWith("STATUS:")) {
      run_state = tok.substring(7);
    }

    if (sp < 0) break;
    start = sp + 1;
  }

  applyFaultGuard();
}

// ─── OLED update ─────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: short label + state
  display.print("gen_a [");
  display.print(run_state);
  display.println("]");

  // Line 2: IP + RSSI
  display.print("IP:");
  display.print(WiFi.localIP().toString());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Line 3: fuel
  display.print("Fuel: ");
  display.print(fuel_pct);
  display.println("%");

  // Line 4: RPM
  display.print("RPM:  ");
  display.println(rpm);

  // Line 5: output voltage
  display.print("Vout: ");
  display.print((int)output_v);
  display.println("V");

  // Line 6: MQTT status + message count
  display.print("MQTT:");
  display.print(mqtt.connected() ? "OK" : "ERR");
  display.print(" Msgs:");
  display.println(message_count);

  display.display();
}

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
  display.println("generator_a");
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
                   "\",\"fuel_pct\":"  + String(fuel_pct) +
                   ",\"rpm\":"         + String(rpm) +
                   ",\"output_v\":"    + String(output_v, 1) +
                   ",\"load_pct\":"    + String(load_pct) +
                   ",\"state\":\""     + run_state + "\"" +
                   ",\"voltage\":"     + String(voltage_rating) +
                   "}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  Serial.println(payload);

  delay(5000);
}
