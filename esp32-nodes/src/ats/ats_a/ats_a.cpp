// ats_a.cpp — Automatic Transfer Switch, 480 V, Side A.
// States: UTILITY, GENERATOR, OPEN, FAULT
#include <winter_river.h>

static const char *NODE_ID = "ats_a";

static constexpr int VOLTAGE_RATING = 480;

static String power_source = "UTILITY";
static float  input_v      = 480.0f;
static float  output_v     = 480.0f;
static int    load_pct     = 35;
static String state        = "UTILITY";

static void handleToken(const String &tok) {
  if (tok == "SOURCE:UTILITY") {
    power_source = "UTILITY"; input_v = 480.0f; output_v = 480.0f; state = "UTILITY";
  } else if (tok == "SOURCE:GENERATOR") {
    power_source = "GENERATOR"; input_v = 480.0f; output_v = 480.0f; state = "GENERATOR";
  } else if (tok == "SOURCE:OPEN") {
    power_source = "OPEN"; input_v = 0.0f; output_v = 0.0f; state = "OPEN";
  } else if (tok.startsWith("LOAD:")) {
    load_pct = tok.substring(5).toInt();
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
}

static void renderDisplay() {
  wr::displayHeader(NODE_ID, state);
  wr::displayNetLine();
  wr::display.print(F("Src: ")); wr::display.println(power_source);
  wr::display.print(F("Vin:  ")); wr::display.print((int)input_v); wr::display.println(F("V"));
  wr::display.print(F("Load: ")); wr::display.print(load_pct);     wr::display.println(F("%"));
  wr::displayFooter();
  wr::display.display();
}

void setup() { wr::begin(NODE_ID, onMqtt); }

void loop() {
  if (!wr::mqttReconnect(NODE_ID)) { delay(2000); return; }
  wr::message_count++;
  renderDisplay();
  wr::mqtt.loop();

  String payload = String("{\"ts\":\"") + wr::timestamp() +
                   "\",\"source\":\""  + power_source + "\"" +
                   ",\"input_v\":"     + String(input_v, 1) +
                   ",\"output_v\":"    + String(output_v, 1) +
                   ",\"load_pct\":"    + String(load_pct) +
                   ",\"state\":\""     + state + "\"" +
                   ",\"voltage\":"     + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
