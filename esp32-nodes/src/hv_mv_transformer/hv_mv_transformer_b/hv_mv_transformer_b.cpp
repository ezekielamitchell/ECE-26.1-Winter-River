// hv_mv_transformer_b.cpp — 230 kV → 34.5 kV step-down transformer, Side B.
// First transformer in the Side B power chain. Sits between utility_b and
// mv_switchgear_b. Mirror of hv_mv_transformer_a. States: NORMAL, WARNING, FAULT.
#include <winter_river.h>

static const char *NODE_ID = "hv_mv_transformer_b";
static const char *LABEL   = "hv_trf_b";

static constexpr int   OUTPUT_KV       = 35;       // 34.5 kV (nominal MV bus)
static constexpr float INPUT_KV_NOM    = 230.0f;   // 230 kV utility input
static constexpr int   CAPACITY_MVA    = 50;       // 50 MVA nameplate

static int    load_pct  = 35;
static float  power_mva = 17.5f;
static int    temp_f    = 105;
static float  input_kv  = INPUT_KV_NOM;
static String state     = "NORMAL";

static void applyGuard() {
  if (load_pct > 95 || temp_f > 200 || input_kv < 100.0f) state = "FAULT";
  else if (load_pct > 80 || temp_f > 160)                 state = "WARNING";
}

static void handleToken(const String &tok) {
  if (tok.startsWith("LOAD:")) {
    load_pct  = tok.substring(5).toInt();
    power_mva = (load_pct / 100.0f) * CAPACITY_MVA;
  } else if (tok.startsWith("TEMP:")) {
    temp_f = tok.substring(5).toInt();
  } else if (tok.startsWith("INPUT_KV:")) {
    input_kv = tok.substring(9).toFloat();
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
  wr::display.print(F("In: "));   wr::display.print(input_kv, 0);  wr::display.println(F("kV"));
  wr::display.print(F("Out: "));  wr::display.print(OUTPUT_KV);    wr::display.print(F("kV  L:"));
  wr::display.print(load_pct);    wr::display.println(F("%"));
  wr::display.print(F("Temp: ")); wr::display.print(temp_f);       wr::display.println(F("F"));
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
                   "\",\"input_kv\":"   + String(input_kv, 1) +
                   ",\"output_kv\":"    + String(OUTPUT_KV) +
                   ",\"load_pct\":"     + String(load_pct) +
                   ",\"power_mva\":"    + String(power_mva, 1) +
                   ",\"temp_f\":"       + String(temp_f) +
                   ",\"state\":\""      + state + "\"" +
                   ",\"voltage\":"      + String(OUTPUT_KV * 1000) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
