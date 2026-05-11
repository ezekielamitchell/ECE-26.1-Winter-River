// cooling_a.cpp — CRAC/CRAH fan bank, 480 V, Side A.
// Simulates 55 fans. Side A + Side B → 110 fans total feeding the thermal model.
// States: NORMAL, DEGRADED, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "cooling_a";
static const char *LABEL   = "cool_a";

static constexpr int VOLTAGE_RATING = 480;
static constexpr int FAN_COUNT      = 55;   // physical fans modeled by this node

static float  input_v        = 480.0f;
static int    coolant_temp_f = 65;
static int    fan_speed_pct  = 60;
static int    fans_running   = FAN_COUNT;
static int    load_pct       = 60;
static String state          = "NORMAL";

static void recomputeFanState() {
  if (fans_running < 0)             fans_running = 0;
  if (fans_running > FAN_COUNT)     fans_running = FAN_COUNT;

  // Fan-bank degradation overrides input-derived state but never improves it.
  if (input_v < 48.0f) {
    state = "OFF";
  } else if (fans_running == 0) {
    state = "FAULT";
  } else if (fans_running < (FAN_COUNT * 8) / 10) {   // <80 % running
    if (state != "FAULT") state = "DEGRADED";
  }
}

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
  } else if (tok.startsWith("FANS_RUNNING:")) {
    fans_running = tok.substring(13).toInt();
    recomputeFanState();
  } else if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
    if (state == "FAULT") fans_running = 0;
    if (state == "OFF")   input_v = 0.0f;
  }
}

static void onMqtt(char *, byte *p, unsigned int l) {
  wr::forEachToken(p, l, handleToken);
  recomputeFanState();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("Vin: "));  wr::display.print((int)input_v);  wr::display.print(F("V "));
  wr::display.print(F("Spd:")); wr::display.print(fan_speed_pct); wr::display.println(F("%"));
  wr::display.print(F("Fans:")); wr::display.print(fans_running); wr::display.print(F("/"));
  wr::display.println(FAN_COUNT);
  wr::display.print(F("CoolT:")); wr::display.print(coolant_temp_f); wr::display.println(F("F"));
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
                   ",\"fan_count\":"        + String(FAN_COUNT) +
                   ",\"fans_running\":"     + String(fans_running) +
                   ",\"load_pct\":"         + String(load_pct) +
                   ",\"state\":\""          + state + "\"" +
                   ",\"voltage\":"          + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
