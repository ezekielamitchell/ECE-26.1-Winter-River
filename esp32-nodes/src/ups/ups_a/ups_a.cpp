#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ** NODE
const char *node_id = "ups_a";
const int voltage_rating = 480; // volts

// ** SIMULATED METRICS (controllable via MQTT)
int    battery_pct  = 100;      // battery charge %
int    load_pct     = 40;       // output load %
float  input_v      = 480.0;    // AC input voltage
float  output_v     = 480.0;    // AC output voltage
String charge_state = "NORMAL"; // NORMAL, ON_BATTERY, CHARGING, FAULT

// ** NETWORK
const char *ssid        = "WinterRiver-AP";
const char *password    = "winterriver";
const char *mqtt_server = "192.168.4.1";

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

// ** OLED (128x64 SSD1306, I2C address 0x3C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if      (msg.startsWith("BATT:"))   battery_pct  = msg.substring(5).toInt();
  else if (msg.startsWith("LOAD:"))   load_pct     = msg.substring(5).toInt();
  else if (msg.startsWith("INPUT:"))  input_v      = msg.substring(6).toFloat();
  else if (msg.startsWith("STATUS:")) charge_state = msg.substring(7);

  if (battery_pct < 10 || input_v < 400.0) {
    charge_state = "FAULT";
  } else if (battery_pct < 25 || input_v < 440.0) {
    charge_state = "ON_BATTERY";
  }
}

void setup() {
  Serial.begin(115200);

  // OLED first — before WiFi radio to avoid I2C interference
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting...");
  display.display();

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
      display.clearDisplay(); display.setCursor(0, 0);
      display.println("WiFi FAILED"); display.println("status=" + String(s)); display.println("Wait 30s...");
      display.display();
      delay(30000); ESP.restart();
    }
    delay(500); Serial.print(".");
  }

  display.clearDisplay(); display.setCursor(0, 0);
  display.println(node_id); display.println("WiFi OK");
  display.display();
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

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
    } else {
      Serial.println("failed, state=" + String(mqtt.state()));
      display.clearDisplay(); display.setCursor(0, 0);
      display.println("MQTT FAILED"); display.println("state=" + String(mqtt.state()));
      display.display();
      delay(2000); return;
    }
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(node_id); display.print(" ["); display.print(charge_state); display.println("]");
  display.print("IP:"); display.print(WiFi.localIP()); display.print(" "); display.print(WiFi.RSSI()); display.println("dB");
  display.print("Batt:   "); display.print(battery_pct); display.println("%");
  display.print("Load:   "); display.print(load_pct); display.println("%");
  display.print("Vin:    "); display.print((int)input_v); display.println("V");
  display.print("MQTT:"); display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:"); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();

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
  delay(5000);
}
