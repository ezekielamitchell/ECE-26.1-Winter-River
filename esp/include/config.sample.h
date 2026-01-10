// Sample Configuration for ESP32 Firmware
// Copy this file to config.h (which is git-ignored) and customize

#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"

// MQTT Broker Configuration
#define MQTT_BROKER "192.168.1.100"  // IP address of your Raspberry Pi
#define MQTT_PORT 1883
#define MQTT_USER ""  // Leave empty if no authentication
#define MQTT_PASSWORD ""

// Device Configuration
#define DEVICE_ID "esp32_node_01"  // Unique identifier for this node
#define MQTT_CLIENT_ID "ESP32Client_01"

// MQTT Topics
#define TOPIC_TELEMETRY "iot/node/01/telemetry"
#define TOPIC_COMMAND "iot/node/01/command"
#define TOPIC_STATUS "iot/node/01/status"

// Timing Configuration
#define TELEMETRY_INTERVAL 5000  // Send telemetry every 5 seconds (ms)
#define MQTT_RECONNECT_INTERVAL 5000  // Retry MQTT connection every 5 seconds (ms)

// Pin Definitions (customize for your hardware)
#define LED_PIN 2  // Built-in LED on most ESP32 boards

// Instructions:
// 1. Copy this file to esp/include/config.h
// 2. Update values according to your setup
// 3. Never commit config.h to version control

#endif // CONFIG_H
