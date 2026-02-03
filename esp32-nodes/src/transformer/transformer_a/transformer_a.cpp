#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_wpa2.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ** NODE
const char *node_id = "transformer_a";
const int voltage_rating = 480;// volts

// ** NETWORK CONFIG
// Set to true for SU-SECURE (WPA2-Enterprise), false for iPhone hotspot (WPA2-Personal)
#define USE_ENTERPRISE_WIFI false

#if USE_ENTERPRISE_WIFI
  const char *ssid = "SU-secure";
  const char *eap_username = "emitchell4";
  const char *eap_password = "EndrCompany0702$$";
#else
  const char *ssid = "Ezekiel's iPhone";
  const char *wifi_password = "01082022";
#endif

// ** MQTT BROKER
const char *mqtt_server = "10.0.0.75"; // change to broker ip

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ** LCD Init (16 columns, 2 rows, I2C address 0x27 - try 0x3F if this doesn't work)
LiquidCrystal_I2C lcd(0x3F, 16, 2);
int message_count = 0;

void setup() {
  Serial.begin(115200);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  // WiFi setup
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

#if USE_ENTERPRISE_WIFI
  // WPA2-Enterprise (SU-SECURE)
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)eap_username, strlen(eap_username));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)eap_username, strlen(eap_username));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)eap_password, strlen(eap_password));
  esp_wifi_sta_wpa2_ent_enable();
  WiFi.begin(ssid);
#else
  // WPA2-Personal (iPhone hotspot)
  WiFi.begin(ssid, wifi_password);
#endif

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