#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ** NODE
const char *node_id = "srv_a";
const int voltage_rating = 208; // volts (standard rack PDU output)

// ** SIMULATED METRICS (controllable via MQTT)
int   cpu_load_pct = 42;       // aggregate CPU utilization %
int   inlet_temp_f = 75;       // rack inlet temperature (°F)
float power_kw     = 3.2;      // total rack power draw (kW)
int   units_active = 8;        // number of active server units
String srv_state   = "NORMAL"; // NORMAL, THROTTLED, FAULT

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

  if (msg.startsWith("CPU:")) {
    cpu_load_pct = msg.substring(4).toInt();
    power_kw     = 1.2 + (cpu_load_pct / 100.0) * 6.0;
  } else if (msg.startsWith("TEMP:")) {
    inlet_temp_f = msg.substring(5).toInt();
  } else if (msg.startsWith("UNITS:")) {
    units_active = msg.substring(6).toInt();
  } else if (msg.startsWith("STATUS:")) {
    srv_state = msg.substring(7);
  }

  if (inlet_temp_f > 95 || cpu_load_pct > 95) {
    srv_state = "FAULT";
  } else if (inlet_temp_f > 85 || cpu_load_pct > 80) {
    srv_state = "THROTTLED";
  } else {
    srv_state = "NORMAL";
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
  display.print(node_id); display.print(" ["); display.print(srv_state); display.println("]");
  display.print("IP:"); display.print(WiFi.localIP()); display.print(" "); display.print(WiFi.RSSI()); display.println("dB");
  display.print("CPU:    "); display.print(cpu_load_pct); display.println("%");
  display.print("Temp:   "); display.print(inlet_temp_f); display.println("F");
  display.print("Power:  "); display.print(power_kw); display.println("kW");
  display.print("MQTT:"); display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:"); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();

  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"cpu_pct\":"   + cpu_load_pct +
                   ",\"inlet_f\":"     + inlet_temp_f +
                   ",\"power_kw\":"    + power_kw     +
                   ",\"units\":"       + units_active +
                   ",\"state\":\""     + srv_state    +
                   "\",\"voltage\":"   + voltage_rating + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  delay(5000);
}
