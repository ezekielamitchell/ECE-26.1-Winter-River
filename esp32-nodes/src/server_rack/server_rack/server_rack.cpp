// ============================================================
// server_rack.cpp — 48V DC Server Rack (2N Shared Load)
// Chain: rectifier_a + rectifier_b → [server_rack]
// Node ID: server_rack | 48V DC, 2N redundant
// ECE 26.1 Winter River — Seattle University
//
// NOTE: This is the only node without a side suffix.
//       It receives dual-path feeds from both Side A and Side B.
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
const int  voltage_rating = 48;   // DC
int        cpu_load_pct   = 42;
int        inlet_temp_f   = 75;
float      power_kw       = 3.2;
int        units_active   = 8;
bool       path_a_ok      = true;   // rectifier_a path alive
bool       path_b_ok      = true;   // rectifier_b path alive
String     srv_state      = "NORMAL";  // NORMAL, DEGRADED, FAULT

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
    if (inlet_temp_f > 95 || cpu_load_pct > 95) {
        srv_state = "FAULT";
    } else if (!path_a_ok || !path_b_ok) {
        srv_state = "DEGRADED";
    } else if (inlet_temp_f > 85 || cpu_load_pct > 80) {
        srv_state = "DEGRADED";
    } else {
        srv_state = "NORMAL";
    }
}

// ── MQTT callback ─────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    // Parse space-separated tokens so compound commands work.
    // e.g. "TOKEN1:val1 TOKEN2:val2" sets both fields.
    bool status_set = false;
    int start = 0;
    while (start <= (int)msg.length()) {
        int sp = msg.indexOf(' ', start);
        String tok = (sp < 0) ? msg.substring(start) : msg.substring(start, sp);

        if (tok.startsWith("CPU:")) {
            cpu_load_pct = tok.substring(4).toInt();
            power_kw     = 1.2f + (cpu_load_pct / 100.0f) * 6.0f;
        } else if (tok.startsWith("TEMP:")) {
            inlet_temp_f = tok.substring(5).toInt();
        } else if (tok.startsWith("UNITS:")) {
            units_active = tok.substring(6).toInt();
        } else if (tok.startsWith("PATH_A:")) {
            path_a_ok = (tok.substring(7).toInt() == 1);
        } else if (tok.startsWith("PATH_B:")) {
            path_b_ok = (tok.substring(7).toInt() == 1);
        } else if (tok.startsWith("STATUS:")) {
            srv_state = tok.substring(7);
            status_set = true;
        }

        if (sp < 0) break;
        start = sp + 1;
    }

    if (!status_set) updateState();
}

// ── OLED draw ────────────────────────────────────────────────
void drawDisplay(bool mqtt_ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: abbreviated node id + state (full "server_rack" is 11 chars, fits at size 1)
    display.setCursor(0, 0);
    display.print("srvr [");
    display.print(srv_state);
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

    // Line 3: CPU load + inlet temperature
    display.setCursor(0, 20);
    display.print("CPU:");
    display.print(cpu_load_pct);
    display.print("% Temp:");
    display.print(inlet_temp_f);
    display.print("F");

    // Line 4: power + active units
    display.setCursor(0, 30);
    display.print("Power:");
    display.print(power_kw, 1);
    display.print("kW U:");
    display.print(units_active);

    // Line 5: dual-path status
    display.setCursor(0, 40);
    display.print("PathA:");
    display.print(path_a_ok ? "OK" : "NO");
    display.print(" PathB:");
    display.print(path_b_ok ? "OK" : "NO");

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
    display.println("server_rack starting...");
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
        String lwt_topic = "winter-river/server_rack/status";
        String lwt_msg   = "{\"node\":\"server_rack\",\"status\":\"OFFLINE\"}";

        if (!mqtt.connect("server_rack", NULL, NULL,
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
                            "\",\"node\":\"server_rack\",\"status\":\"ONLINE\"}";
        mqtt.publish("winter-river/server_rack/status", online_msg.c_str(), true);

        // Subscribe to control topic
        mqtt.subscribe("winter-river/server_rack/control");
        Serial.println("MQTT connected, subscribed to winter-river/server_rack/control");
    }

    mqtt.loop();

    // Build and publish telemetry
    String ts      = getTimestamp();
    String payload = "{\"ts\":\"" + ts + "\""
                   + ",\"cpu_pct\":"  + String(cpu_load_pct)
                   + ",\"inlet_f\":"  + String(inlet_temp_f)
                   + ",\"power_kw\":" + String(power_kw, 1)
                   + ",\"units\":"    + String(units_active)
                   + ",\"path_a\":"   + String(path_a_ok ? 1 : 0)
                   + ",\"path_b\":"   + String(path_b_ok ? 1 : 0)
                   + ",\"state\":\""  + srv_state + "\""
                   + ",\"voltage\":"  + String(voltage_rating)
                   + "}";

    mqtt.publish("winter-river/server_rack/status", payload.c_str());
    Serial.println("Published: " + payload);

    message_count++;
    drawDisplay(mqtt.connected());

    delay(5000);
}
