#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ** NODE
const char *node_id = "gen_a";
const int voltage_rating = 480; // volts output

// ** SIMULATED METRICS (controllable via MQTT)
int   fuel_pct   = 85;        // fuel tank level %
int   rpm        = 0;         // engine RPM (0 = off)
float output_v   = 0.0;       // generator output voltage
int   load_pct   = 0;         // output load %
String run_state = "STANDBY"; // STANDBY, STARTING, RUNNING, FAULT

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

// ** OLED (128x64 SSD1306 — address 0x3C common, 0x3D if SA0 pin is HIGH)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

// Try 0x3C first, fall back to 0x3D — both are valid SSD1306 addresses
uint8_t detectOLEDAddr() {
  for (uint8_t addr : {0x3C, 0x3D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3C; // fallback
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("FUEL:")) {
    fuel_pct = msg.substring(5).toInt();
  } else if (msg.startsWith("RPM:")) {
    rpm      = msg.substring(4).toInt();
    output_v = (rpm > 0) ? voltage_rating : 0.0;
  } else if (msg.startsWith("LOAD:")) {
    load_pct = msg.substring(5).toInt();
  } else if (msg.startsWith("STATUS:")) {
    run_state = msg.substring(7);
  }

  if (fuel_pct < 5 || (run_state == "RUNNING" && rpm < 800)) {
    run_state = "FAULT";
  } else if (rpm > 1500) {
    run_state = "RUNNING";
    output_v  = voltage_rating;
  } else if (rpm > 0) {
    run_state = "STARTING";
  } else {
    run_state = "STANDBY";
    output_v  = 0.0;
  }
}

void setup() {
  Serial.begin(115200);

  // OLED first — before WiFi radio to avoid I2C interference
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, detectOLEDAddr());
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
  display.print(node_id); display.print(" ["); display.print(run_state); display.println("]");
  display.print("IP:"); display.print(WiFi.localIP()); display.print(" "); display.print(WiFi.RSSI()); display.println("dB");
  display.print("Fuel: "); display.print(fuel_pct); display.println("%");
  display.print("RPM:  "); display.println(rpm);
  display.print("Vout: "); display.print((int)output_v); display.println("V");
  display.print("MQTT:"); display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:"); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();

  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"fuel_pct\":"  + fuel_pct  +
                   ",\"rpm\":"         + rpm        +
                   ",\"output_v\":"    + output_v   +
                   ",\"load_pct\":"    + load_pct   +
                   ",\"state\":\""     + run_state  +
                   "\",\"voltage\":"   + voltage_rating + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  delay(5000);
}
