#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ** NODE
const char *node_id = "server";
const int voltage_rating = 480;// volts

// ** NETWORK
const char *ssid = "WinterRiver-AP";
const char *password = "winterriver";

// ** MQTT BROKER
const char *mqtt_server = "192.168.4.1"; // Pi hotspot gateway

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** LCD Init (16 columns, 2 rows, I2C address 0x27 - try 0x3F if this doesn't work)
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

void setup() {
  Serial.begin(115200);

  // WiFi setup — full radio reset before connecting
  WiFi.mode(WIFI_OFF);       // power down radio
  delay(100);                // let it settle
  WiFi.mode(WIFI_STA);       // back to station mode
  WiFi.disconnect(true);     // clear any cached credentials
  delay(100);
  WiFi.begin(ssid, password);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_start > 15000) {  // 15 s timeout
      Serial.println("\nWiFi failed (status=" + String(WiFi.status()) + ") — restarting in 3 s");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi FAILED");
      lcd.setCursor(0, 1);
      lcd.print("status=" + String(WiFi.status()));
      delay(3000);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(node_id);
  lcd.print(" OK");

  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  mqtt.setServer(mqtt_server, 1883);
}

void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    if (mqtt.connect(node_id)) {
      Serial.println("connected");
    } else {
      Serial.println("failed");
      delay(2000);
      return;
    }
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(node_id);
  message_count++;
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  lcd.print(" ");
  lcd.print(voltage_rating);

  mqtt.loop();
  String msg = String("MQTT Relay Successful: ") + node_id;
  mqtt.publish("winter-river", msg.c_str());
  Serial.println("Message sent");
  delay(1000);
}