// ============================================================
// cooling_b.cpp — Cooling System (CRAC/CRAH), Side B
// Chain: lv_dist_b → [cooling_b]
// Node ID: cooling_b | 480V Cooling
// ECE 26.1 Winter River — Seattle University
// ============================================================

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ── Network constants ────────────────────────────────────────
const char* ssid        = "WinterRiver-AP";
const char* password    = "winterriver";
const char* mqtt_server = "192.168.4.1";

// ── NTP ─────────────────────────────────────────────────────
const char* ntp_server          = "192.168.4.1";
const long  gmt_offset_sec      = -28800;
const int   daylight_offset_sec = 3600;

// ── OLED ─────────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── MQTT ─────────────────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ── Node state ───────────────────────────────────────────────
const int  voltage_rating  = 480;
float      input_v         = 480.0;
int        coolant_temp_f  = 65;
int        fan_speed_pct   = 60;
int        load_pct        = 60;
String     cool_state      = "NORMAL";  // NORMAL, DEGRADED, FAULT, OFF

// ── Message counter ──────────────────────────────────────────
int message_count = 0;

// ── Helpers ──────────────────────────────────────────────────
String getTimestamp() {
    struct tm ti;
    if (!getLocalTime(&ti)) return "00:00:00";
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
}

void updateTempState() {
    if (coolant_temp_f > 80) {
        cool_state = "FAULT";
    } else if (coolant_temp_f > 72) {
        cool_state = "DEGRADED";
    }
}

// ── MQTT callback ─────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    if (msg.startsWith("INPUT:")) {
        input_v = msg.substring(6).toFloat();
        if (input_v < 48.0) {
            cool_state = "OFF";
        } else if (cool_state == "OFF") {
            cool_state = "NORMAL";
        }
    } else if (msg.startsWith("TEMP:")) {
        coolant_temp_f = msg.substring(5).toInt();
        if (coolant_temp_f > 80) {
            cool_state = "FAULT";
        } else if (coolant_temp_f > 72) {
            cool_state = "DEGRADED";
        }
    } else if (msg.startsWith("SPEED:")) {
        fan_speed_pct = msg.substring(6).toInt();
        load_pct = fan_speed_pct;
    } else if (msg.startsWith("STATUS:")) {
        cool_state = msg.substring(7);
    }
}

// ── OLED draw ────────────────────────────────────────────────
void drawDisplay(bool mqtt_ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: node id + state
    display.setCursor(0, 0);
    display.print("cool_b [");
    display.print(cool_state);
    display.print("]");

    // Line 2: IP + RSSI
    display.setCursor(0, 10);
    if (WiFi.status() == WL_CONNECTED) {
        display.print("IP:");
        display.print(WiFi.localIP().toString());
        display.print(" ");
        display.print(WiFi.RSSI());
        display.print("dB");
    } else {
        display.print("WiFi disconnected");
    }

    // Line 3: input voltage
    display.setCursor(0, 20);
    display.print("Vin:  ");
    display.print((int)input_v);
    display.print("V");

    // Line 4: coolant temperature
    display.setCursor(0, 30);
    display.print("CoolT: ");
    display.print(coolant_temp_f);
    display.print("F");

    // Line 5: fan speed
    display.setCursor(0, 40);
    display.print("Fan:  ");
    display.print(fan_speed_pct);
    display.print("%");

    // Line 6: MQTT status + message count
    display.setCursor(0, 54);
    display.print("MQTT:");
    display.print(mqtt_ok ? "OK" : "ERR");
    display.print(" Msgs:");
    display.print(message_count);

    display.display();
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // OLED before WiFi (prevent I2C interference)
    Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        for (;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("cooling_b starting...");
    display.display();

    // WiFi reset sequence
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(200);
    WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi FAILED");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("WiFi FAILED");
        display.println("SSID: WinterRiver-AP");
        display.println("Retrying in 30s...");
        display.display();
        delay(30000);
        ESP.restart();
    }

    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

    // NTP sync
    configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
    struct tm ti;
    int ntpTries = 0;
    while (!getLocalTime(&ti) && ntpTries < 10) {
        delay(500);
        ntpTries++;
    }

    // MQTT setup
    mqtt.setServer(mqtt_server, 1883);
    mqtt.setCallback(mqttCallback);
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    // MQTT connect / reconnect
    if (!mqtt.connected()) {
        String lwt_topic = "winter-river/cooling_b/status";
        String lwt_msg   = "{\"node\":\"cooling_b\",\"status\":\"OFFLINE\"}";

        if (!mqtt.connect("cooling_b", NULL, NULL,
                          lwt_topic.c_str(), 1, true, lwt_msg.c_str())) {
            Serial.println("MQTT FAILED, rc=" + String(mqtt.state()));
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("MQTT FAILED");
            display.print("rc=");
            display.println(mqtt.state());
            display.display();
            delay(2000);
            return;
        }

        // Publish ONLINE
        String online_msg = "{\"ts\":\"" + getTimestamp() +
                            "\",\"node\":\"cooling_b\",\"status\":\"ONLINE\"}";
        mqtt.publish("winter-river/cooling_b/status", online_msg.c_str(), true);

        // Subscribe to control topic
        mqtt.subscribe("winter-river/cooling_b/control");
        Serial.println("MQTT connected, subscribed to winter-river/cooling_b/control");
    }

    mqtt.loop();

    // Build and publish telemetry
    String ts      = getTimestamp();
    String payload = "{\"ts\":\"" + ts + "\""
                   + ",\"input_v\":"        + String(input_v, 1)
                   + ",\"coolant_temp_f\":" + String(coolant_temp_f)
                   + ",\"fan_speed_pct\":"  + String(fan_speed_pct)
                   + ",\"load_pct\":"       + String(load_pct)
                   + ",\"state\":\""        + cool_state + "\""
                   + ",\"voltage\":"        + String(voltage_rating)
                   + "}";

    mqtt.publish("winter-river/cooling_b/status", payload.c_str());
    Serial.println("Published: " + payload);

    message_count++;
    drawDisplay(mqtt.connected());

    delay(5000);
}
