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

// Set true when a control message carried an explicit STATUS: token, so the
// broker's authoritative state is not second-guessed by the local guard below.
// Mirrors the status_set pattern in server_rack.cpp.
static bool status_set = false;

static void handleToken(const String &tok) {
  if      (tok.startsWith("BATT:"))   battery_pct = tok.substring(5).toInt();
  else if (tok.startsWith("LOAD:"))   load_pct    = tok.substring(5).toInt();
  else if (tok.startsWith("INPUT:"))  input_v     = tok.substring(6).toFloat();
  else if (tok.startsWith("STATUS:")) { state = tok.substring(7); status_set = true; }
}

// Standalone fallback only. When main.py is driving it sends an explicit STATUS
// every tick (NORMAL / CHARGING / ON_BATTERY / FAULT) and that wins — otherwise a
// low-but-charging battery (input restored, battery still <25%) would be wrongly
// pinned to ON_BATTERY here, masking the CHARGING recovery.
static void applyGuard() {
  if (battery_pct < 10 || input_v < 400.0f)      state = "FAULT";
  else if (battery_pct < 25 || input_v < 440.0f) state = "ON_BATTERY";
}

static void onMqtt(char *, byte *p, unsigned int l) {
  status_set = false;
  wr::forEachToken(p, l, handleToken);
  if (!status_set) applyGuard();
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
  if (!wr::mqttReconnect(NODE_ID)) { delay(1000); return; }
  wr::mqtt.loop();  // pump every iteration: drain queued control + service keepalive
  if (!wr::dueForTelemetry()) { delay(10); return; }
  wr::message_count++;
  renderDisplay();

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
}
