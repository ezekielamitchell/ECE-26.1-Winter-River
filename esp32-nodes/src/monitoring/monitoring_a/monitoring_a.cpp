// monitoring_a.cpp — BMS/sensor monitoring, 120 V, Side A.
// States: NORMAL, ALERT, FAULT, OFF
#include <winter_river.h>

static const char *NODE_ID = "monitoring_a";
static const char *LABEL   = "mon_a";

static constexpr int VOLTAGE_RATING = 120;

static float  input_v      = 120.0f;
static int    sensor_count = 12;
static int    alert_count  = 0;
static int    uptime_pct   = 100;
static int    load_pct     = 15;
static String state        = "NORMAL";

static void recalcMetrics() {
  if (sensor_count < 0) sensor_count = 0;
  load_pct   = sensor_count + 3;
  if (load_pct > 100) load_pct = 100;
  uptime_pct = (input_v < 100.0f) ? 0 : 100;

  if (state == "NORMAL" || state == "OFF") alert_count = 0;
  else if (alert_count < 1)                alert_count = 1;
}

static void updateState() {
  if (input_v > 145.0f)      state = "FAULT";
  else if (input_v < 100.0f) state = "OFF";
  else if (alert_count > 0)  state = "ALERT";
  else                       state = "NORMAL";
  recalcMetrics();
}

static void applyForcedState(const String &forced_state) {
  state = forced_state;
  if (state == "OFF") {
    input_v = 0.0f;
  } else if (input_v < 100.0f) {
    input_v = (float)VOLTAGE_RATING;
  }
  recalcMetrics();
}

static void handleToken(const String &tok, bool &status_set) {
  if (tok.startsWith("INPUT:")) {
    input_v = tok.substring(6).toFloat();
  } else if (tok.startsWith("SENSORS:")) {
    sensor_count = tok.substring(8).toInt();
    recalcMetrics();
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
  wr::display.print(F("Sens: ")); wr::display.print(sensor_count); wr::display.print(F(" Alrt:"));
  wr::display.println(alert_count);
  wr::display.print(F("Up:   ")); wr::display.print(uptime_pct);   wr::display.println(F("%"));
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
                   "\",\"input_v\":"      + String(input_v, 1) +
                   ",\"sensor_count\":"   + String(sensor_count) +
                   ",\"alert_count\":"    + String(alert_count) +
                   ",\"uptime_pct\":"     + String(uptime_pct) +
                   ",\"load_pct\":"       + String(load_pct) +
                   ",\"state\":\""        + state + "\"" +
                   ",\"voltage\":"        + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
  delay(wr::TELEMETRY_INTERVAL_MS);
}
