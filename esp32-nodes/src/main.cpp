#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi Connection Credentials
const char* ssid = "endr";
const char* password = "SeattleUniversity01$$";



void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("\nConnecting...");

  while (WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("ESP32 initialized");
}

void loop() {
  Serial.println("Hello Winter River");
  delay(2000);
}
