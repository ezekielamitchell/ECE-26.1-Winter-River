#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ** NODE
const char *node_id = "A";


// ** NETWORK
const char *ssid = "endr";       // change to local wifi ssid
const char *password = "SeattleUniversity01$$"; // change to local wifi pass
const char *mqtt_server = "10.0.0.75"; // change to broker ip

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** LCD Init (16 columns, 2 rows, I2C address 0x27 - try 0x3F if this doesn't work)
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NODE_A Connected");

  Serial.println("\nWiFi connected");

  mqtt.setServer(mqtt_server, 1883);
}

void loop() {
  if (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    if (mqtt.connect("ESP32")) {
      Serial.println("connected");
    } else {
      Serial.println("failed");
      delay(2000);
      return;
    }
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Node: A | ct: ");
  lcd.print(message_count);
  message_count++;
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());

  mqtt.loop();
  mqtt.publish("winter-river", "MQTT Relay Successful: NodeA");
  Serial.println("Message sent");
  delay(5000);
}