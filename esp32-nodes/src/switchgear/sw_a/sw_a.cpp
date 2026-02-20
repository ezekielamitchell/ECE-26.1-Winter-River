#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ** NODE
const char *node_id = "sw_a";
const int voltage_rating = 480; // volts

// ** SIMULATED METRICS (controllable via MQTT)
bool  breaker_closed = true;    // main breaker state
float current_a      = 120.5;   // current draw (amps)
float load_kw        = 86.5;    // active power (kW)
int   load_pct       = 35;      // % of rated capacity
String sw_state      = "CLOSED"; // CLOSED, OPEN, TRIPPED, FAULT

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

// ** LCD — pointer, allocated in setup() after I2C address scan
// Scanning 0x27 and 0x3F covers both common PCF8574 backpack variants
LiquidCrystal_I2C *lcd = nullptr;

uint8_t detectLCDAddr() {
  for (uint8_t addr : {0x27, 0x3F}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3F; // fallback
}

int message_count = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg == "OPEN") {
    breaker_closed = false;
    sw_state = "OPEN";
  } else if (msg == "CLOSE") {
    breaker_closed = true;
    sw_state = "CLOSED";
  } else if (msg.startsWith("LOAD:")) {
    load_pct  = msg.substring(5).toInt();
    load_kw   = load_pct * 2.47;   // scale to rated kW
    current_a = (load_kw * 1000.0) / voltage_rating;
  } else if (msg.startsWith("STATUS:")) {
    sw_state = msg.substring(7);
  }

  // Auto-thresholds
  if (current_a > 280.0 || load_pct > 95) {
    sw_state       = "TRIPPED";
    breaker_closed = false;
  } else if (current_a > 220.0 || load_pct > 80) {
    sw_state = "FAULT";
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
  lcd->print(" ");
  lcd->print(sw_state.substring(0, 7));
  lcd->setCursor(0, 1);
  lcd->print((int)current_a);
  lcd->print("A ");
  lcd->print(load_pct);
  lcd->print("%");

  mqtt.loop();

  // Telemetry
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"breaker\":"   + (breaker_closed ? "true" : "false") +
                   ",\"current_a\":"   + current_a +
                   ",\"load_kw\":"     + load_kw   +
                   ",\"load_pct\":"    + load_pct  +
                   ",\"state\":\""     + sw_state  +
                   "\",\"voltage\":"   + voltage_rating + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;
  delay(5000);
}
