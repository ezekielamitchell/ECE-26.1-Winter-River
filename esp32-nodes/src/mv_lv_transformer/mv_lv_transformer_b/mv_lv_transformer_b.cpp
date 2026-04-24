// mv_lv_transformer_b.cpp — 34.5 kV → 480 V transformer, 1000 kVA, Side B.
// States: NORMAL, WARNING, FAULT
#include <winter_river.h>

static const char *NODE_ID = "mv_lv_transformer_b";
static const char *LABEL   = "mv_trf_b";

static constexpr int VOLTAGE_RATING = 480;
static constexpr int CAPACITY_KVA   = 1000;

static int    load_pct  = 45;
static float  power_kva = 450.0f;
static int    temp_f    = 112;
static String state     = "NORMAL";

static void applyGuard() {
  if (load_pct > 90 || temp_f > 185)      state = "FAULT";
  else if (load_pct > 75 || temp_f > 149) state = "WARNING";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("LOAD:")) {
    load_pct  = tok.substring(5).toInt();
    power_kva = (load_pct / 100.0f) * CAPACITY_KVA;
  } else if (tok.startsWith("TEMP:")) {
    temp_f = tok.substring(5).toInt();
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
  wr::display.print(F("Load: ")); wr::display.print(load_pct);
  wr::display.print(F("% (")); wr::display.print((int)power_kva); wr::display.println(F("kVA)"));
  wr::display.print(F("Temp: ")); wr::display.print(temp_f); wr::display.println(F(" F"));
  wr::display.print(VOLTAGE_RATING); wr::display.println(F("V out"));
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
                   "\",\"load_pct\":"  + String(load_pct) +
                   ",\"power_kva\":"  + String(power_kva, 1) +
                   ",\"temp_f\":"     + String(temp_f) +
                   ",\"state\":\""    + state + "\"" +
                   ",\"voltage\":"    + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
