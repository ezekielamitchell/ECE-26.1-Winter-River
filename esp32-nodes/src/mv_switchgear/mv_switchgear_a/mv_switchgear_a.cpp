// mv_switchgear_a.cpp — MV switchgear, Side A (34.5 kV).
// States: CLOSED, OPEN, TRIPPED, FAULT
#include <winter_river.h>

static const char *NODE_ID = "mv_switchgear_a";
static const char *LABEL   = "mv_sw_a";

static constexpr int VOLTAGE_RATING = 34500;

static bool   breaker_closed = true;
static float  current_a      = 15.2f;
static float  load_kw        = 420.0f;
static int    load_pct       = 30;
static String state          = "CLOSED";

static void applyGuard() {
  if (current_a > 40.0f || load_pct > 95) {
    state = "TRIPPED";
    breaker_closed = false;
  } else if (current_a > 32.0f || load_pct > 80) {
    state = "FAULT";
  }
}

static void handleToken(const String &tok) {
  if (tok == "CLOSE") {
    breaker_closed = true; state = "CLOSED";
  } else if (tok == "OPEN") {
    breaker_closed = false; state = "OPEN";
  } else if (tok.startsWith("LOAD:")) {
    load_pct  = tok.substring(5).toInt();
    load_kw   = load_pct * 14.0f;
    current_a = (load_kw * 1000.0f) / VOLTAGE_RATING;
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  applyGuard();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("Current: ")); wr::display.print((int)current_a); wr::display.println(F("A"));
  wr::display.print(F("Load:    ")); wr::display.print(load_pct);       wr::display.println(F("%"));
  wr::display.print(F("Vout: "));    wr::display.print(VOLTAGE_RATING); wr::display.println(F("V"));
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
                   "\",\"breaker\":" + String(breaker_closed ? "true" : "false") +
                   ",\"current_a\":" + String(current_a, 1) +
                   ",\"load_kw\":"   + String(load_kw, 1) +
                   ",\"load_pct\":"  + String(load_pct) +
                   ",\"state\":\""   + state + "\"" +
                   ",\"voltage\":"   + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
