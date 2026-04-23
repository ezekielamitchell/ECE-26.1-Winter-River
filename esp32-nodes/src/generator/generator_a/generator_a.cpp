// generator_a.cpp — 480 V diesel standby generator, Side A.
// States: STANDBY, STARTING, RUNNING, FAULT
#include <winter_river.h>

static const char *NODE_ID = "generator_a";
static const char *LABEL   = "gen_a";

static constexpr int VOLTAGE_RATING = 480;

static int    fuel_pct = 85;
static int    rpm      = 0;
static float  output_v = 0.0f;
static int    load_pct = 0;
static String state    = "STANDBY";

static void deriveStateFromRPM() {
  if (rpm > 1500)    { state = "RUNNING";  output_v = VOLTAGE_RATING; }
  else if (rpm > 0)  { state = "STARTING"; output_v = 0.0f; }
  else               { state = "STANDBY";  output_v = 0.0f; }
}

static void applyFaultGuard() {
  if (fuel_pct < 5 || (state == "RUNNING" && rpm < 800)) state = "FAULT";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("FUEL:")) {
    fuel_pct = tok.substring(5).toInt();
  } else if (tok.startsWith("RPM:")) {
    rpm = tok.substring(4).toInt();
    output_v = (rpm > 0) ? VOLTAGE_RATING : 0.0f;
    deriveStateFromRPM();
  } else if (tok.startsWith("LOAD:")) {
    load_pct = tok.substring(5).toInt();
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  applyFaultGuard();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("Fuel: ")); wr::display.print(fuel_pct);     wr::display.println(F("%"));
  wr::display.print(F("RPM:  ")); wr::display.println(rpm);
  wr::display.print(F("Vout: ")); wr::display.print((int)output_v); wr::display.println(F("V"));
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
                   "\",\"fuel_pct\":" + String(fuel_pct) +
                   ",\"rpm\":"        + String(rpm) +
                   ",\"output_v\":"   + String(output_v, 1) +
                   ",\"load_pct\":"   + String(load_pct) +
                   ",\"state\":\""    + state + "\"" +
                   ",\"voltage\":"    + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
