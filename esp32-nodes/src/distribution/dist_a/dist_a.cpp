#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ─── NODE IDENTITY ────────────────────────────────────────────────────────────
// dist_a: Main LV Distribution Board — Side A
// 480 V distribution hub between sw_a/gen_a ATS output and ups_a.
// Chain: util_a → trf_a → sw_a → [dist_a] → ups_a → pdu_a → srv_a
//                         gen_a ↗
// Rated: 480 V, 800 A, 384 kW (0.9 PF)
// ─────────────────────────────────────────────────────────────────────────────

const char *node_id = "dist_a";

const float RATED_VOLTAGE  = 480.0;
const float RATED_POWER_KW = 384.0;

float  input_v      = 480.0;
float  ups_load_kw  = 95.0;
float  mech_load_kw = 42.0;
float  total_load_kw = 137.0;
int    load_pct     = 36;
String power_source = "UTILITY"; // UTILITY | GENERATOR | NONE
String dist_state   = "NORMAL";  // NORMAL | OVERLOAD | FAULT | NO_INPUT

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

// Commands: INPUT:xxx | UPS:xxx | MECH:xxx | SOURCE:UTILITY/GENERATOR/NONE | STATUS:xxx
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("INPUT:")) {
    input_v = msg.substring(6).toFloat();
    if (input_v < RATED_VOLTAGE * 0.10) { dist_state = "NO_INPUT"; power_source = "NONE"; }
    else if (input_v >= RATED_VOLTAGE * 0.90 && dist_state == "NO_INPUT") dist_state = "NORMAL";
  } else if (msg.startsWith("UPS:")) {
    ups_load_kw   = msg.substring(4).toFloat();
    total_load_kw = ups_load_kw + mech_load_kw;
    load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0);
  } else if (msg.startsWith("MECH:")) {
    mech_load_kw  = msg.substring(5).toFloat();
    total_load_kw = ups_load_kw + mech_load_kw;
    load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0);
  } else if (msg.startsWith("SOURCE:")) {
    power_source = msg.substring(7);
    if (power_source == "NONE") { dist_state = "NO_INPUT"; input_v = 0.0; }
    else if (dist_state == "NO_INPUT") { dist_state = "NORMAL"; input_v = RATED_VOLTAGE; }
  } else if (msg.startsWith("STATUS:")) {
    dist_state = msg.substring(7);
  }

  if      (input_v < RATED_VOLTAGE * 0.10) dist_state = "NO_INPUT";
  else if (load_pct > 95)                   dist_state = "OVERLOAD";
  else if (load_pct > 85)                   dist_state = "FAULT";
  else if (dist_state != "NO_INPUT")        dist_state = "NORMAL";
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
  display.print(node_id); display.print(" ["); display.print(dist_state); display.println("]");
  display.print("IP:"); display.print(WiFi.localIP()); display.print(" "); display.print(WiFi.RSSI()); display.println("dB");
  display.print("Vin:  "); display.print((int)input_v); display.println("V");
  display.print("Load: "); display.print((int)total_load_kw); display.print("kW "); display.print(load_pct); display.println("%");
  display.print("Src:  "); display.println(power_source);
  display.print("MQTT:"); display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:"); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();

  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"")      + getTimestamp()  +
                   "\",\"input_v\":"         + input_v         +
                   ",\"ups_load_kw\":"       + ups_load_kw     +
                   ",\"mech_load_kw\":"      + mech_load_kw    +
                   ",\"total_load_kw\":"     + total_load_kw   +
                   ",\"load_pct\":"          + load_pct        +
                   ",\"source\":\""          + power_source    +
                   "\",\"state\":\""         + dist_state      +
                   "\",\"voltage\":"         + RATED_VOLTAGE   + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;
  delay(5000);
}
