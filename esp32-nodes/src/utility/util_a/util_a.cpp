#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ─── NODE IDENTITY ────────────────────────────────────────────────────────────
// util_a: MV Utility Feed — Side A
// Root of the entire Side-A power hierarchy. No parent node.
// Disconnect triggers full cascade failure (Step 2 demo scenario).
// Chain: [util_a] → trf_a → sw_a → gen_a → dist_a → ups_a → pdu_a → srv_a
// Vout: 230 kV MV feed | 60 Hz | 3-phase
// ─────────────────────────────────────────────────────────────────────────────

const char *node_id = "util_a";

const float GRID_VOLTAGE_KV = 230.0;
const float FREQ_HZ         = 60.0;
const int   PHASE_COUNT     = 3;

float  grid_voltage_kv = 230.0;
float  freq_hz         = 60.0;
int    load_pct        = 12;
String grid_state      = "GRID_OK"; // GRID_OK, SAG, SWELL, OUTAGE, FAULT

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

WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ** OLED (128x64 SSD1306, I2C address 0x3C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

// Commands: STATUS:GRID_OK/SAG/SWELL/OUTAGE | VOLT:xxx.x | FREQ:xx.x | LOAD:xx
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("STATUS:")) {
    grid_state = msg.substring(7);
    if      (grid_state == "OUTAGE") grid_voltage_kv = 0.0;
    else if (grid_state == "SAG")    grid_voltage_kv = GRID_VOLTAGE_KV * 0.88;
    else if (grid_state == "SWELL")  grid_voltage_kv = GRID_VOLTAGE_KV * 1.10;
    else                             grid_voltage_kv = GRID_VOLTAGE_KV;
  } else if (msg.startsWith("VOLT:")) {
    grid_voltage_kv = msg.substring(5).toFloat();
    if      (grid_voltage_kv == 0.0)                    grid_state = "OUTAGE";
    else if (grid_voltage_kv < GRID_VOLTAGE_KV * 0.90)  grid_state = "SAG";
    else if (grid_voltage_kv > GRID_VOLTAGE_KV * 1.10)  grid_state = "SWELL";
    else                                                 grid_state = "GRID_OK";
  } else if (msg.startsWith("FREQ:")) {
    freq_hz = msg.substring(5).toFloat();
    if (freq_hz < 59.3 || freq_hz > 60.7) grid_state = "FAULT";
  } else if (msg.startsWith("LOAD:")) {
    load_pct = msg.substring(5).toInt();
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
  display.print(node_id); display.print(" ["); display.print(grid_state); display.println("]");
  display.print("IP:"); display.print(WiFi.localIP()); display.print(" "); display.print(WiFi.RSSI()); display.println("dB");
  display.print("Vout: "); display.print((int)grid_voltage_kv); display.println("kV");
  display.print("Freq: "); display.print(freq_hz); display.println("Hz");
  display.print("Load: "); display.print(load_pct); display.println("%");
  display.print("MQTT:"); display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:"); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();

  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"")       + getTimestamp()  +
                   "\",\"v_out\":"            + grid_voltage_kv +
                   ",\"freq_hz\":"            + freq_hz         +
                   ",\"load_pct\":"           + load_pct        +
                   ",\"state\":\""            + grid_state      +
                   "\",\"voltage_kv\":"       + GRID_VOLTAGE_KV +
                   ",\"phase\":"              + PHASE_COUNT     + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;
  delay(5000);
}
