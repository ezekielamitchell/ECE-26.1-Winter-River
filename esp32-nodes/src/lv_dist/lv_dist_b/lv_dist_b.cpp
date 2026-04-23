// lv_dist_b.cpp — 480 V LV distribution, 384 kW rated, Side B.
// States: NORMAL, OVERLOAD, FAULT, NO_INPUT
#include <winter_river.h>

static const char *NODE_ID = "lv_dist_b";

static constexpr float RATED_VOLTAGE  = 480.0f;
static constexpr float RATED_POWER_KW = 384.0f;

static float  input_v       = 480.0f;
static float  ups_load_kw   = 95.0f;
static float  mech_load_kw  = 42.0f;
static float  total_load_kw = 137.0f;
static int    load_pct      = 36;
static String power_source  = "UTILITY";
static String state         = "NORMAL";

static void recalcLoad() {
  total_load_kw = ups_load_kw + mech_load_kw;
  load_pct      = (int)((total_load_kw / RATED_POWER_KW) * 100.0f);
}

static void applyGuard() {
  if (input_v < 48.0f)          state = "NO_INPUT";
  else if (load_pct > 95)       state = "OVERLOAD";
  else if (load_pct > 85)       state = "FAULT";
  else if (state != "NO_INPUT") state = "NORMAL";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("INPUT:")) {
    input_v = tok.substring(6).toFloat();
    if (input_v < 48.0f)                               { state = "NO_INPUT"; power_source = "NONE"; }
    else if (input_v >= 432.0f && state == "NO_INPUT")   state = "NORMAL";
  } else if (tok.startsWith("UPS:")) {
    ups_load_kw = tok.substring(4).toFloat();
    recalcLoad();
  } else if (tok.startsWith("MECH:")) {
    mech_load_kw = tok.substring(5).toFloat();
    recalcLoad();
  } else if (tok.startsWith("SOURCE:")) {
    power_source = tok.substring(7);
    if (power_source == "NONE")         { state = "NO_INPUT"; input_v = 0.0f; }
    else if (state == "NO_INPUT")       { state = "NORMAL"; input_v = RATED_VOLTAGE; }
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  applyGuard();
}

static void renderDisplay() {
  wr::displayHeader(NODE_ID, state);
  wr::displayNetLine();
  wr::display.print(F("Vin:  ")); wr::display.print((int)input_v);      wr::display.println(F("V"));
  wr::display.print(F("Load: ")); wr::display.print((int)total_load_kw);
  wr::display.print(F("kW "));    wr::display.print(load_pct);          wr::display.println(F("%"));
  wr::display.print(F("Src: "));  wr::display.println(power_source);
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
                   ",\"ups_load_kw\":"      + String(ups_load_kw, 1) +
                   ",\"mech_load_kw\":"     + String(mech_load_kw, 1) +
                   ",\"total_load_kw\":"    + String(total_load_kw, 1) +
                   ",\"load_pct\":"         + String(load_pct) +
                   ",\"source\":\""         + power_source + "\"" +
                   ",\"state\":\""          + state + "\"" +
                   ",\"voltage\":"          + String(RATED_VOLTAGE, 1) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
