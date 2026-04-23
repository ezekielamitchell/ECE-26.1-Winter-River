// cooling_b.cpp — CRAC/CRAH cooling unit, 480 V, Side B.
// States: NORMAL, DEGRADED, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "cooling_b";
static const char *LABEL   = "cool_b";

static constexpr int VOLTAGE_RATING = 480;

static float  input_v        = 480.0f;
static int    coolant_temp_f = 65;
static int    fan_speed_pct  = 60;
static int    load_pct       = 60;
static String state          = "NORMAL";

static void handleToken(const String &tok) {
  if (tok.startsWith("INPUT:")) {
    input_v = tok.substring(6).toFloat();
    if (input_v < 48.0f)         state = "OFF";
    else if (state == "OFF")     state = "NORMAL";
  } else if (tok.startsWith("TEMP:")) {
    coolant_temp_f = tok.substring(5).toInt();
    if (coolant_temp_f > 80)       state = "FAULT";
    else if (coolant_temp_f > 72)  state = "DEGRADED";
  } else if (tok.startsWith("SPEED:")) {
    fan_speed_pct = tok.substring(6).toInt();
    load_pct      = fan_speed_pct;
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("Vin:  "));  wr::display.print((int)input_v);  wr::display.println(F("V"));
  wr::display.print(F("CoolT: ")); wr::display.print(coolant_temp_f); wr::display.println(F("F"));
  wr::display.print(F("Fan:  "));  wr::display.print(fan_speed_pct);  wr::display.println(F("%"));
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
                   "\",\"input_v\":"        + String(input_v, 1) +
                   ",\"coolant_temp_f\":"   + String(coolant_temp_f) +
                   ",\"fan_speed_pct\":"    + String(fan_speed_pct) +
                   ",\"load_pct\":"         + String(load_pct) +
                   ",\"state\":\""          + state + "\"" +
                   ",\"voltage\":"          + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
