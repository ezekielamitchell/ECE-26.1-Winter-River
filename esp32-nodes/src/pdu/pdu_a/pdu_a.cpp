// pdu_a.cpp — 480 V PDU, Side A.
// States: NORMAL, OVERLOAD, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "pdu_a";

static constexpr int VOLTAGE_RATING = 480;

static float  input_v  = 480.0f;
static float  output_v = 480.0f;
static int    load_pct = 25;
static String state    = "NORMAL";

static void updateState() {
  if (load_pct > 95)                                     state = "OVERLOAD";
  else if (load_pct > 85)                                state = "FAULT";
  else if (input_v < 48.0f)                              state = "OFF";
  else if (state != "OVERLOAD" && state != "FAULT")      state = "NORMAL";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("INPUT:")) {
    input_v = tok.substring(6).toFloat();
    if (input_v < 48.0f)         state = "OFF";
    else if (state == "OFF")     state = "NORMAL";
    output_v = (load_pct > 0 && input_v > 0) ? input_v : 0.0f;
  } else if (tok.startsWith("LOAD:")) {
    load_pct = tok.substring(5).toInt();
    output_v = (load_pct > 0 && input_v > 0) ? input_v : 0.0f;
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  updateState();
}

static void renderDisplay() {
  wr::displayHeader(NODE_ID, state);
  wr::displayNetLine();
  wr::display.print(F("Vin:  ")); wr::display.print((int)input_v);  wr::display.println(F("V"));
  wr::display.print(F("Load: ")); wr::display.print(load_pct);      wr::display.println(F("%"));
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
                   "\",\"input_v\":"  + String(input_v, 1) +
                   ",\"output_v\":"   + String(output_v, 1) +
                   ",\"load_pct\":"   + String(load_pct) +
                   ",\"state\":\""    + state + "\"" +
                   ",\"voltage\":"    + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
