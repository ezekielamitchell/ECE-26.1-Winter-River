// ============================================================
// lighting_b.cpp — Lighting Circuit, Side B
// Chain: lv_dist_b → [lighting_b]
// Node ID: lighting_b | 277V Lighting (phase-to-neutral)
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

// Probe 0x3C then 0x3D — returns whichever ACKs, defaults to 0x3C
uint8_t detectOLEDAddr() {
  for (uint8_t addr : {0x3C, 0x3D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0x3C;
}

// ── MQTT ─────────────────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ── Node state ───────────────────────────────────────────────
const int  voltage_rating = 277;
float      input_v        = 277.0;
int        load_pct       = 45;
int        dimmer_pct     = 80;
String     light_state    = "ON";  // ON, OFF, FAULT

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

// ── MQTT callback ─────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    // Parse space-separated tokens so compound commands work.
    // e.g. "TOKEN1:val1 TOKEN2:val2" sets both fields.
    int start = 0;
    while (start <= (int)msg.length()) {
        int sp = msg.indexOf(' ', start);
        String tok = (sp < 0) ? msg.substring(start) : msg.substring(start, sp);

        if (tok.startsWith("INPUT:")) {
            input_v = tok.substring(6).toFloat();
            if (input_v < 240.0) {
                light_state = "OFF";
            } else if (light_state == "OFF") {
                light_state = "ON";
            }
        } else if (tok.startsWith("DIM:")) {
            dimmer_pct = tok.substring(4).toInt();
            load_pct   = (int)(dimmer_pct * 0.56f);
        } else if (tok.startsWith("STATUS:")) {
            String val = tok.substring(7);
            if (val == "ON") {
                light_state = "ON";
            } else if (val == "OFF") {
                light_state = "OFF";
                input_v = 0.0;
            } else {
                light_state = val;
            }
        }

        if (sp < 0) break;
        start = sp + 1;
    }
}

// ── OLED draw ────────────────────────────────────────────────
void drawDisplay(bool mqtt_ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: node id + state
    display.setCursor(0, 0);
    display.print("light_b [");
    display.print(light_state);
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

    // Line 4: dimmer level
    display.setCursor(0, 30);
    display.print("Dim:  ");
    display.print(dimmer_pct);
    display.print("%");

    // Line 5: load
    display.setCursor(0, 40);
    display.print("Load: ");
    display.print(load_pct);
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
    uint8_t oledAddr = detectOLEDAddr();
    Serial.print("OLED addr: 0x"); Serial.println(oledAddr, HEX);
    if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
        Serial.println("SSD1306 allocation failed");
        for (;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("lighting_b starting...");
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
        String lwt_topic = "winter-river/lighting_b/status";
        String lwt_msg   = "{\"node\":\"lighting_b\",\"status\":\"OFFLINE\"}";

        if (!mqtt.connect("lighting_b", NULL, NULL,
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
                            "\",\"node\":\"lighting_b\",\"status\":\"ONLINE\"}";
        mqtt.publish("winter-river/lighting_b/status", online_msg.c_str(), true);

        // Subscribe to control topic
        mqtt.subscribe("winter-river/lighting_b/control");
        Serial.println("MQTT connected, subscribed to winter-river/lighting_b/control");
    }

    mqtt.loop();

    // Build and publish telemetry
    String ts      = getTimestamp();
    String payload = "{\"ts\":\"" + ts + "\""
                   + ",\"input_v\":"    + String(input_v, 1)
                   + ",\"load_pct\":"   + String(load_pct)
                   + ",\"dimmer_pct\":" + String(dimmer_pct)
                   + ",\"state\":\""    + light_state + "\""
                   + ",\"voltage\":"    + String(voltage_rating)
                   + "}";

    mqtt.publish("winter-river/lighting_b/status", payload.c_str());
    Serial.println("Published: " + payload);

    message_count++;
    drawDisplay(mqtt.connected());

    delay(5000);
}
