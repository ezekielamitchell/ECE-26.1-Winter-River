#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ** NODE
const char *node_id = "pdu_a";
const int voltage_rating = 480;// volts

// ** NETWORK
const char *ssid = "WinterRiver-AP";
const char *password = "winterriver";

// ** MQTT BROKER
const char *mqtt_server = "192.168.4.1"; // Pi hotspot gateway

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

  // WiFi setup — full radio reset before connecting
  WiFi.persistent(false);    // do NOT read/write NVS credentials
  WiFi.mode(WIFI_OFF);       // power down radio
  delay(200);                // let it settle
  WiFi.mode(WIFI_STA);       // back to station mode
  WiFi.disconnect(false);    // clear state without blocking reconnect
  delay(200);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);  // accept WPA as well as WPA2
  WiFi.begin(ssid, password);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_start > 20000) {  // 20 s timeout
      int s = WiFi.status();
      Serial.println("\nWiFi failed (status=" + String(s) + ") — waiting 30 s for hotspot then restarting");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi FAILED s=");
      lcd.print(s);
      lcd.setCursor(0, 1);
      lcd.print("Wait 30s...");
      delay(30000);
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
      Serial.println("failed, state=" + String(mqtt.state()));
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