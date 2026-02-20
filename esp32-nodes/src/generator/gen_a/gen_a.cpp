#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ** NODE
const char *node_id = "gen_a";
const int voltage_rating = 480; // volts output

// ** SIMULATED METRICS (controllable via MQTT)
int   fuel_pct    = 85;         // fuel tank level %
int   rpm         = 0;          // engine RPM (0 = off)
float output_v    = 0.0;        // generator output voltage
int   load_pct    = 0;          // output load %
String run_state  = "STANDBY";  // STANDBY, STARTING, RUNNING, FAULT

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

// ** LCD (16x2, I2C) — address auto-detected at boot (0x27 or 0x3F)
uint8_t lcd_addr = 0x3F;
LiquidCrystal_I2C lcd(lcd_addr, 16, 2);
int message_count = 0;

// Scan both common LCD backpack addresses; fall back to 0x3F if neither found
uint8_t detectLCDAddr() {
  for (uint8_t addr : {0x27, 0x3F}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3F;
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

  // Auto-thresholds
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

  // Initialize LCD first — before WiFi radio starts to avoid I2C interference
  Wire.begin();
  lcd_addr = detectLCDAddr();
  lcd = LiquidCrystal_I2C(lcd_addr, 16, 2);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  // WiFi — full radio reset
  WiFi.persistent(false);    // do NOT read/write NVS credentials
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);    // clear state without blocking reconnect
  delay(200);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  WiFi.begin(ssid, password);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_start > 20000) {
      int s = WiFi.status();
      Serial.println("\nWiFi failed (status=" + String(s) + ") — waiting 30 s for hotspot then restarting");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi FAILED s="); lcd.print(s);
      lcd.setCursor(0,1); lcd.print("Wait 30s...");
      delay(30000); ESP.restart();
    }
    delay(500); Serial.print(".");
  }

  lcd.clear(); lcd.setCursor(0,0);
  lcd.print(node_id); lcd.print(" OK");
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
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(node_id);
  lcd.print(" F:");
  lcd.print(fuel_pct);
  lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print(run_state.substring(0, 8));
  lcd.print(" ");
  lcd.print(rpm);
  lcd.print("rpm");

  mqtt.loop();

  // Telemetry
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
  message_count++;
  delay(5000);
}
