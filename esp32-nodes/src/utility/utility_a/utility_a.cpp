// utility_a.cpp — Root utility grid, Side A (230 kV / 60 Hz / 3-phase).
// States: GRID_OK, SAG, SWELL, OUTAGE, FAULT
#include <winter_river.h>

static const char *NODE_ID = "utility_a";

static constexpr float NOMINAL_KV = 230.0f;
static constexpr int   PHASE_COUNT = 3;

static float  voltage_kv = NOMINAL_KV;
static float  freq_hz    = 60.0f;
static int    load_pct   = 12;
static String state      = "GRID_OK";

static void handleToken(const String &tok) {
  if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
    if      (state == "OUTAGE") voltage_kv = 0.0f;
    else if (state == "SAG")    voltage_kv = NOMINAL_KV * 0.88f;
    else if (state == "SWELL")  voltage_kv = NOMINAL_KV * 1.10f;
    else                        voltage_kv = NOMINAL_KV;
  } else if (tok.startsWith("VOLT:")) {
    voltage_kv = tok.substring(5).toFloat();
    float ratio = voltage_kv / NOMINAL_KV;
    if      (voltage_kv <= 0.0f) state = "OUTAGE";
    else if (ratio < 0.90f)      state = "SAG";
    else if (ratio > 1.10f)      state = "SWELL";
    else                         state = "GRID_OK";
  } else if (tok.startsWith("FREQ:")) {
    freq_hz = tok.substring(5).toFloat();
    if (freq_hz < 59.3f || freq_hz > 60.7f) state = "FAULT";
  } else if (tok.startsWith("LOAD:")) {
    load_pct = tok.substring(5).toInt();
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
}

static void renderDisplay() {
  wr::displayHeader(NODE_ID, state);
  wr::displayNetLine();
  wr::display.print(F("Vout: ")); wr::display.print(voltage_kv, 0); wr::display.println(F("kV"));
  wr::display.print(F("Freq: ")); wr::display.print(freq_hz, 2);    wr::display.println(F("Hz"));
  wr::display.print(F("Load: ")); wr::display.print(load_pct);      wr::display.println(F("%"));
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
                   "\",\"v_out\":"     + String(voltage_kv, 1) +
                   ",\"freq_hz\":"     + String(freq_hz, 1) +
                   ",\"load_pct\":"    + String(load_pct) +
                   ",\"state\":\""     + state + "\"" +
                   ",\"voltage_kv\":"  + String(voltage_kv, 1) +
                   ",\"phase\":"       + String(PHASE_COUNT) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
