// ups_b.cpp — 480 V UPS, Side B.
// States: NORMAL, ON_BATTERY, CHARGING, FAULT
#include <winter_river.h>

static const char *NODE_ID = "ups_b";

static constexpr int VOLTAGE_RATING = 480;

static int    battery_pct = 100;
static int    load_pct    = 40;
static float  input_v     = 480.0f;
static float  output_v    = 480.0f;
static String state       = "NORMAL";

static void handleToken(const String &tok) {
  if      (tok.startsWith("BATT:"))   battery_pct = tok.substring(5).toInt();
  else if (tok.startsWith("LOAD:"))   load_pct    = tok.substring(5).toInt();
  else if (tok.startsWith("INPUT:"))  input_v     = tok.substring(6).toFloat();
  else if (tok.startsWith("STATUS:")) state       = tok.substring(7);
}

static void applyGuard() {
  if (battery_pct < 10 || input_v < 400.0f)      state = "FAULT";
  else if (battery_pct < 25 || input_v < 440.0f) state = "ON_BATTERY";
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  applyGuard();
}

static void renderDisplay() {
  wr::displayHeader(NODE_ID, state);
  wr::displayNetLine();
  wr::display.print(F("Batt: ")); wr::display.print(battery_pct);   wr::display.println(F("%"));
  wr::display.print(F("Load: ")); wr::display.print(load_pct);      wr::display.println(F("%"));
  wr::display.print(F("Vin:  ")); wr::display.print((int)input_v);  wr::display.println(F("V"));
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
                   "\",\"battery_pct\":" + String(battery_pct) +
                   ",\"load_pct\":"      + String(load_pct) +
                   ",\"input_v\":"       + String(input_v, 1) +
                   ",\"output_v\":"      + String(output_v, 1) +
                   ",\"state\":\""       + state + "\"" +
                   ",\"voltage\":"       + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
