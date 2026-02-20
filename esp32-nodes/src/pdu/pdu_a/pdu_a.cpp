#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ** NODE
const char *node_id = "pdu_a";
const int voltage_rating = 480; // volts

// ** NETWORK
const char *ssid        = "WinterRiver-AP";
const char *password    = "winterriver";
const char *mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** OLED (128x64 SSD1306, I2C address 0x3C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int message_count = 0;

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

  mqtt.setServer(mqtt_server, 1883);
}

void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    if (mqtt.connect(node_id)) {
      Serial.println("connected");
    } else {
      Serial.println("failed, state=" + String(mqtt.state()));
      delay(2000); return;
    }
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(node_id);
  display.print("IP: "); display.println(WiFi.localIP());
  display.print("Volt: "); display.print(voltage_rating); display.println("V");
  display.print("Msgs: "); display.println(message_count);
  display.display();
  message_count++;

  mqtt.loop();
  String msg = String("MQTT Relay Successful: ") + node_id;
  mqtt.publish("winter-river", msg.c_str());
  Serial.println("Message sent");
  delay(1000);
}
