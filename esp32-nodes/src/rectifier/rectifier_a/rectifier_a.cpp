// ============================================================
// rectifier_a.cpp — AC/DC Rectifier, Side A
// Chain: pdu_a → [rectifier_a] → server_rack
// Node ID: rectifier_a | 480V AC → 48V DC
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
const int  ac_voltage_rating = 480;
const int  dc_voltage_rating = 48;
float      input_v_ac  = 480.0;
float      output_v_dc = 48.0;
int        load_pct    = 30;
String     rect_state  = "NORMAL";  // NORMAL, FAULT, OFF

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

void updateState() {
    if (input_v_ac > 528.0) {
        rect_state = "FAULT";
    } else if (input_v_ac < 400.0) {
        rect_state = "OFF";
    }
    // NORMAL is set explicitly on recovery from OFF in callback
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

        if (tok.startsWith("INPUT_AC:")) {
            input_v_ac = tok.substring(9).toFloat();
            output_v_dc = (input_v_ac > 400.0) ? (float)dc_voltage_rating : 0.0;
            if (input_v_ac < 400.0) {
                rect_state = "OFF";
            } else if (rect_state == "OFF") {
                rect_state = "NORMAL";
            }
        } else if (tok.startsWith("LOAD:")) {
            load_pct = tok.substring(5).toInt();
        } else if (tok.startsWith("STATUS:")) {
            rect_state = tok.substring(7);
        }

        if (sp < 0) break;
        start = sp + 1;
    }

    updateState();
}

// ── OLED draw ────────────────────────────────────────────────
void drawDisplay(bool mqtt_ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: node id + state
    display.setCursor(0, 0);
    display.print("rect_a [");
    display.print(rect_state);
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

    // Line 3: AC input voltage
    display.setCursor(0, 20);
    display.print("ACin: ");
    display.print((int)input_v_ac);
    display.print("V");

    // Line 4: DC output voltage
    display.setCursor(0, 30);
    display.print("DCout:  ");
    display.print((int)output_v_dc);
    display.print("V");

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
    display.println("rectifier_a starting...");
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
        String lwt_topic = "winter-river/rectifier_a/status";
        String lwt_msg   = "{\"node\":\"rectifier_a\",\"status\":\"OFFLINE\"}";

        if (!mqtt.connect("rectifier_a", NULL, NULL,
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
                            "\",\"node\":\"rectifier_a\",\"status\":\"ONLINE\"}";
        mqtt.publish("winter-river/rectifier_a/status", online_msg.c_str(), true);

        // Subscribe to control topic
        mqtt.subscribe("winter-river/rectifier_a/control");
        Serial.println("MQTT connected, subscribed to winter-river/rectifier_a/control");
    }

    mqtt.loop();

    // Build and publish telemetry
    String ts      = getTimestamp();
    String payload = "{\"ts\":\"" + ts + "\""
                   + ",\"input_v_ac\":"  + String(input_v_ac,  1)
                   + ",\"output_v_dc\":" + String(output_v_dc, 1)
                   + ",\"load_pct\":"    + String(load_pct)
                   + ",\"state\":\""     + rect_state + "\""
                   + ",\"ac_voltage\":"  + String(ac_voltage_rating)
                   + ",\"dc_voltage\":"  + String(dc_voltage_rating)
                   + "}";

    mqtt.publish("winter-river/rectifier_a/status", payload.c_str());
    Serial.println("Published: " + payload);

    message_count++;
    drawDisplay(mqtt.connected());

    delay(5000);
}
