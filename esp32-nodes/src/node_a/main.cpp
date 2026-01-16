#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "endr"; // change to local wifi ssid
const char* password = "SeattleUniversity01$$"; // change to local wifi pass
const char* mqtt_server = "10.0.0.76"; // change to broker ip

WiFiClient espClient;
PubSubClient mqtt(espClient);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
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

  mqtt.loop();
  mqtt.publish("winter-river", "MQTT Relay Successful: NodeA");
  Serial.println("Message sent");
  delay(5000);
}
