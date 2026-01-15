#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>

// WiFi credentials - update these for your network
const char* WIFI_SSID = "endr";
const char* WIFI_PASSWORD = "SeattleUniversity01$$";

void setUp(void) {
    // Disconnect any existing connection before each test
    WiFi.disconnect(true);
    delay(100);
}

void tearDown(void) {
    // Clean up after each test
    WiFi.disconnect(true);
}

void test_wifi_connection(void) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();
    const unsigned long timeout = 10000; // 10 second timeout

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
        delay(100);
    }

    TEST_ASSERT_EQUAL(WL_CONNECTED, WiFi.status());
}

void test_wifi_has_valid_ip(void) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();
    const unsigned long timeout = 10000;

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
        delay(100);
    }

    TEST_ASSERT_EQUAL(WL_CONNECTED, WiFi.status());

    IPAddress ip = WiFi.localIP();
    // Verify we got a valid IP (not 0.0.0.0)
    TEST_ASSERT_NOT_EQUAL(0, ip[0]);
}

void test_wifi_signal_strength(void) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();
    const unsigned long timeout = 10000;

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
        delay(100);
    }

    TEST_ASSERT_EQUAL(WL_CONNECTED, WiFi.status());

    int rssi = WiFi.RSSI();
    // RSSI should be negative and reasonable (between -100 and 0 dBm)
    TEST_ASSERT_LESS_THAN(0, rssi);
    TEST_ASSERT_GREATER_THAN(-100, rssi);
}

void setup() {
    delay(2000);

    UNITY_BEGIN();

    RUN_TEST(test_wifi_connection);
    RUN_TEST(test_wifi_has_valid_ip);
    RUN_TEST(test_wifi_signal_strength);

    UNITY_END();
}

void loop() {
}
