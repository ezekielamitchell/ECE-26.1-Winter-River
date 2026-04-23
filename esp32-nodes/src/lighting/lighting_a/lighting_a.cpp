// lighting_a.cpp — Lighting panel, 277 V, Side A.
// States: NORMAL, DIMMED, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "lighting_a";
static const char *LABEL   = "light_a";

static constexpr int VOLTAGE_RATING = 277;
static constexpr int ZONES_ACTIVE   = 4;

static float  input_v         = 277.0f;
static int    load_pct        = 40;
static int    brightness_pct  = 100;
static String state           = "NORMAL";

static void recalcLoad() {
  if (brightness_pct < 0) brightness_pct = 0;
  if (brightness_pct > 100) brightness_pct = 100;
  load_pct = (brightness_pct * 40 + 50) / 100;
}

static void updateState() {
  if (input_v < 240.0f || brightness_pct == 0)      state = "OFF";
  else if (input_v > 305.0f)                        state = "FAULT";
  else if (brightness_pct < 100)                    state = "DIMMED";
  else                                              state = "NORMAL";
}

static void applyForcedState(const String &forced_state) {
  state = forced_state;
  if (state == "OFF") {
    input_v = 0.0f;
  } else if (state == "NORMAL") {
    if (input_v < 240.0f) input_v = (float)VOLTAGE_RATING;
    brightness_pct = 100;
    recalcLoad();
  } else if (state == "DIMMED") {
    if (input_v < 240.0f) input_v = (float)VOLTAGE_RATING;
    if (brightness_pct >= 100) brightness_pct = 80;
    recalcLoad();
  }
}

static void handleToken(const String &tok, bool &status_set) {
  if (tok.startsWith("INPUT:")) {
    input_v = tok.substring(6).toFloat();
  } else if (tok.startsWith("DIM:")) {
    brightness_pct = tok.substring(4).toInt();
    recalcLoad();
  } else if (tok.startsWith("STATUS:")) {
    applyForcedState(tok.substring(7));
    status_set = true;
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  bool status_set = false;
  String msg;
  msg.reserve(l);
  for (unsigned int i = 0; i < l; i++) msg += (char)p[i];

  int start = 0;
  while (start <= (int)msg.length()) {
    int sp = msg.indexOf(' ', start);
    String tok = (sp < 0) ? msg.substring(start) : msg.substring(start, sp);
    if (tok.length() > 0) handleToken(tok, status_set);
    if (sp < 0) break;
    start = sp + 1;
  }

  if (!status_set) updateState();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("Vin:  ")); wr::display.print((int)input_v); wr::display.println(F("V"));
  wr::display.print(F("Bright:")); wr::display.print(brightness_pct); wr::display.println(F("%"));
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
                   "\",\"input_v\":"    + String(input_v, 1) +
                   ",\"zones_active\":" + String(ZONES_ACTIVE) +
                   ",\"brightness_pct\":"+ String(brightness_pct) +
                   ",\"load_pct\":"     + String(load_pct) +
                   ",\"state\":\""      + state + "\"" +
                   ",\"voltage\":"      + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
