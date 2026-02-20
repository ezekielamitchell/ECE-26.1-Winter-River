#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ** NODE
const char *node_id = "srv_a";
const int voltage_rating = 208; // volts (standard rack PDU output)

// ** SIMULATED METRICS (controllable via MQTT)
int   cpu_load_pct  = 42;       // aggregate CPU utilization %
int   inlet_temp_f  = 75;       // rack inlet temperature (°F)
float power_kw      = 3.2;      // total rack power draw (kW)
int   units_active  = 8;        // number of active server units
String srv_state    = "NORMAL"; // NORMAL, THROTTLED, FAULT

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

// ** LCD (16x2, I2C — try 0x27 if 0x3F shows no text)
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  if (msg.startsWith("CPU:")) {
    cpu_load_pct = msg.substring(4).toInt();
    power_kw     = 1.2 + (cpu_load_pct / 100.0) * 6.0; // scale 1.2–7.2 kW
  } else if (msg.startsWith("TEMP:")) {
    inlet_temp_f = msg.substring(5).toInt();
  } else if (msg.startsWith("UNITS:")) {
    units_active = msg.substring(6).toInt();
  } else if (msg.startsWith("STATUS:")) {
    srv_state = msg.substring(7);
  }

  // Auto-thresholds
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

  // Initialize LCD first — before WiFi radio starts to avoid I2C interference
  Wire.begin();
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
  lcd.print(" C:");
  lcd.print(cpu_load_pct);
  lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print(srv_state.substring(0, 8));
  lcd.print(" ");
  lcd.print(inlet_temp_f);
  lcd.print("F");

  mqtt.loop();

  // Telemetry
  String topic   = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"cpu_pct\":"    + cpu_load_pct +
                   ",\"inlet_f\":"      + inlet_temp_f +
                   ",\"power_kw\":"     + power_kw     +
                   ",\"units\":"        + units_active +
                   ",\"state\":\""      + srv_state    +
                   "\",\"voltage\":"    + voltage_rating + "}";
  mqtt.publish(topic.c_str(), payload.c_str());
  Serial.println("Published: " + payload);
  message_count++;
  delay(5000);
}
