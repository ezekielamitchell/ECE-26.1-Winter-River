#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ** NODE
const char *node_id = "ups_a";
const int voltage_rating = 480; // volts

// ** SIMULATED METRICS (controllable via MQTT)
int battery_pct     = 100;      // battery charge %
int load_pct        = 40;       // output load %
float input_v       = 480.0;    // AC input voltage
float output_v      = 480.0;    // AC output voltage
String charge_state = "NORMAL"; // NORMAL, ON_BATTERY, CHARGING, FAULT

// ** NETWORK
const char *ssid         = "WinterRiver-AP";
const char *password     = "winterriver";
const char *mqtt_server  = "192.168.4.1";

// ** NTP
const char* ntp_server          = "192.168.4.1";
const long  gmt_offset_sec      = -28800;
const int   daylight_offset_sec = 3600;

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** LCD — pointer, allocated in setup() after I2C address scan
LiquidCrystal_I2C *lcd = nullptr;

uint8_t detectLCDAddr() {
  for (uint8_t addr : {0x27, 0x3F}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3F;
}

int message_count = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if      (msg.startsWith("BATT:"))   battery_pct  = msg.substring(5).toInt();
  else if (msg.startsWith("LOAD:"))   load_pct     = msg.substring(5).toInt();
  else if (msg.startsWith("INPUT:"))  input_v      = msg.substring(6).toFloat();
  else if (msg.startsWith("STATUS:")) charge_state = msg.substring(7);

  // Auto-thresholds
  if (battery_pct < 10 || input_v < 400.0) {
    charge_state = "FAULT";
  } else if (battery_pct < 25 || input_v < 440.0) {
    charge_state = "ON_BATTERY";
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize LCD first — before WiFi radio starts to avoid I2C interference
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
      lcd->clear(); lcd->setCursor(0,0); lcd->print("WiFi FAILED s="); lcd->print(s);
      lcd->setCursor(0,1); lcd->print("Wait 30s...");
      delay(30000); ESP.restart();
    }
    delay(500); Serial.print(".");
  }

  lcd->clear(); lcd->setCursor(0,0);
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

void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    String lwt_topic = String("winter-river/") + node_id + "/status";
    String lwt_msg   = String("{\"node\":\"") + node_id + "\",\"status\":\"OFFLINE\"}";
    if (mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_msg.c_str())) {
      Serial.println("connected");
      String online = String("{\"ts\":\"") + getTimestamp() + "\",\"node\":\"" + node_id + "\",\"status\":\"ONLINE\"}";
      mqtt.publish(lwt_topic.c_str(), online.c_str(), true);
      String ctrl = String("winter-river/") + node_id + "/control";
      mqtt.subscribe(ctrl.c_str());
      Serial.println("Subscribed to: " + ctrl);
    } else {
      Serial.println("failed, state=" + String(mqtt.state()));
      delay(2000); return;
    }
  }

  // LCD display
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(node_id);
  lcd->print(" B:");
  lcd->print(battery_pct);
  lcd->print("%");
  lcd->setCursor(0, 1);
  lcd->print(charge_state.substring(0, 8));
  lcd->print(" L:");
  lcd->print(load_pct);
  lcd->print("%");

  mqtt.loop();

  // Telemetry
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"battery_pct\":"  + battery_pct  +
                   ",\"load_pct\":"       + load_pct     +
                   ",\"input_v\":"        + input_v      +
                   ",\"output_v\":"       + output_v     +
                   ",\"state\":\""        + charge_state +
                   "\",\"voltage\":"      + voltage_rating + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;
  delay(5000);
}
