// rectifier_b.cpp — 480 V AC → 48 V DC rectifier, Side B.
// States: NORMAL, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "rectifier_b";
static const char *LABEL   = "rect_b";

static constexpr int AC_VOLTAGE_RATING = 480;
static constexpr int DC_VOLTAGE_RATING = 48;

static float  input_v_ac  = 480.0f;
static float  output_v_dc = 48.0f;
static int    load_pct    = 30;
static String state       = "NORMAL";

static void updateState() {
  if (input_v_ac > 528.0f)      state = "FAULT";
  else if (input_v_ac < 400.0f) state = "OFF";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("INPUT_AC:")) {
    input_v_ac  = tok.substring(9).toFloat();
    output_v_dc = (input_v_ac > 400.0f) ? (float)DC_VOLTAGE_RATING : 0.0f;
    if (input_v_ac < 400.0f)      state = "OFF";
    else if (state == "OFF")      state = "NORMAL";
  } else if (tok.startsWith("LOAD:")) {
    load_pct = tok.substring(5).toInt();
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  updateState();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("ACin: "));   wr::display.print((int)input_v_ac);  wr::display.println(F("V"));
  wr::display.print(F("DCout: "));  wr::display.print((int)output_v_dc); wr::display.println(F("V"));
  wr::display.print(F("Load: "));   wr::display.print(load_pct);         wr::display.println(F("%"));
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
                   "\",\"input_v_ac\":"  + String(input_v_ac, 1) +
                   ",\"output_v_dc\":"   + String(output_v_dc, 1) +
                   ",\"load_pct\":"      + String(load_pct) +
                   ",\"state\":\""       + state + "\"" +
                   ",\"ac_voltage\":"    + String(AC_VOLTAGE_RATING) +
                   ",\"dc_voltage\":"    + String(DC_VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
