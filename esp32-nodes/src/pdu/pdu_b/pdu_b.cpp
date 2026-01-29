#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ** NODE
const char *node_id = "pdu_b";
const int voltage_rating = 480;


// ** NETWORK
const char *ssid = "endr";       // change to local wifi ssid
const char *password = "SeattleUniversity01$$"; // change to local wifi pass
const char *mqtt_server = "10.0.0.75"; // change to broker ip

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** Voltage Read
const int ADC_PIN = 34;


// ** LCD Init (16 columns, 2 rows, I2C address 0x27 - try 0x3F if this doesn't work)
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  adcAttachPin(ADC_PIN); // Configure ADC pin, default is ADC_11b (0-3.3V)
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // *ESP32

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
  lcd.print(node_id);
  lcd.print(" OK");

  Serial.println("\nWiFi connected");

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

  // Average multiple readings to reduce noise
  long sum = 0;
  for (int i = 0; i < 64; i++) {
    sum += analogRead(ADC_PIN);
  }
  int esp_raw_value = sum / 64;
  double voltage = esp_raw_value * (3.3 / 4095.0);

  
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