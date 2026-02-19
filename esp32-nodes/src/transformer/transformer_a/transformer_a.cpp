#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ** NODE
const char *node_id = "trf_a";
const int voltage_rating = 480; // volts
const int capacity_kva = 500;   // transformer capacity

// ** SIMULATED METRICS (controllable via MQTT)
int load_percent = 45;          // % of capacity
float power_kva = 225.0;        // kVA output
int temp_f = 108;         // winding temperature (°F)
String status_str = "NORMAL";   // NORMAL, WARNING, FAULT

// ** NETWORK CONFIG
const char *ssid = "WinterRiver-AP";
const char *wifi_password = "winterriver";

// ** MQTT BROKER
const char *mqtt_server = "192.168.4.1"; // Pi hotspot gateway

// ** NTP CONFIG
const char* ntp_server = "192.168.4.1";  // Pi acts as local NTP server
const long gmt_offset_sec = -28800;  // PST = UTC-8 (adjust for your timezone)
const int daylight_offset_sec = 3600; // DST offset

// Get formatted timestamp: dd/mm/yyyy | hh:mm:ss
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00:00";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** OLED Init (128x64, I2C address 0x3C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

// MQTT callback for receiving control commands
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Received: ");
  Serial.println(message);

  // Parse commands: LOAD:xx, TEMP:xx, STATUS:xxxx
  if (message.startsWith("LOAD:")) {
    load_percent = message.substring(5).toInt();
    power_kva = (load_percent / 100.0) * capacity_kva;
  } else if (message.startsWith("TEMP:")) {
    temp_f = message.substring(5).toInt();
  } else if (message.startsWith("STATUS:")) {
    status_str = message.substring(7);
  }

  // Auto-set warnings based on thresholds
  if (load_percent > 90 || temp_f > 185) {
    status_str = "FAULT";
  } else if (load_percent > 75 || temp_f > 149) {
    status_str = "WARNING";
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting...");
  display.display();

  // WiFi setup
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(node_id);
  display.println("OK");
  display.display();

  Serial.println("\nWiFi connected");

  // Sync time with NTP server
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
  Serial.println("Waiting for NTP time sync...");
  struct tm timeinfo;
  int ntp_retries = 0;
  while (!getLocalTime(&timeinfo) && ntp_retries < 10) {
    delay(500);
    Serial.print(".");
    ntp_retries++;
  }
  if (ntp_retries >= 10) {
    Serial.println("\nNTP sync failed, continuing without time");
  } else {
    Serial.println("\nTime synced: " + getTimestamp());
  }

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    // Last Will and Testament - broker publishes this if node disconnects unexpectedly
    String lwt_topic = String("winter-river/") + node_id + "/status";
    String lwt_message = String("{\"node\":\"") + node_id + "\",\"status\":\"OFFLINE\"}";
    if (mqtt.connect(node_id, lwt_topic.c_str(), 1, true, lwt_message.c_str())) {
      Serial.println("connected");

      // Publish ONLINE status to override retained LWT
      String online_msg = String("{\"ts\":\"") + getTimestamp() + "\",\"node\":\"" + node_id + "\",\"status\":\"ONLINE\"}";
      mqtt.publish(lwt_topic.c_str(), online_msg.c_str(), true);  // retained

      // Subscribe to control topic for this node
      String control_topic = String("winter-river/") + node_id + "/control";
      mqtt.subscribe(control_topic.c_str());
      Serial.print("Subscribed to: ");
      Serial.println(control_topic);
    } else {
      Serial.println("failed");
      delay(2000);
      return;
    }
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);

  // Header with status indicator
  display.print(node_id);
  display.print(" [");
  display.print(status_str);
  display.println("]");

  // Network info
  display.print("IP:");
  display.print(WiFi.localIP());
  display.print(" ");
  display.print(WiFi.RSSI());
  display.println("dB");

  // Transformer metrics
  display.print("Load: ");
  display.print(load_percent);
  display.print("% (");
  display.print((int)power_kva);
  display.println("kVA)");

  display.print("Temp: ");
  display.print(temp_f);
  display.println(" F");

  display.print("Rating: ");
  display.print(voltage_rating);
  display.println("V");

  display.print("MQTT:");
  display.print(mqtt.connected() ? "OK" : "DISC");
  display.print(" Msgs:");
  display.println(message_count);

  display.display();
  message_count++;

  mqtt.loop();

  // Publish status JSON to telemetry topic
  String status_topic = String("winter-river/") + node_id + "/status";
  String payload = String("{\"ts\":\"") + getTimestamp() +
                   "\",\"load\":" + load_percent +
                   ",\"power_kva\":" + power_kva +
                   ",\"temp_f\":" + temp_f +
                   ",\"status\":\"" + status_str +
                   "\",\"voltage\":" + voltage_rating + "}";
  mqtt.publish(status_topic.c_str(), payload.c_str());

  delay(5000); // 5 sec
}