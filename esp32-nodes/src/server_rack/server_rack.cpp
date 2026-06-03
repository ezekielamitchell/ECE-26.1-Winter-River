// server_rack.cpp — 48 V DC IT rack. One source file shared by all 8 racks
// (server_rack_a1..a4, server_rack_b1..b4). NODE_ID and OLED label are
// injected via PlatformIO build_flags:
//   '-DWR_NODE_ID="server_rack_a1"' '-DWR_RACK_LABEL="rack_a1"'
//
// Each rack is single-fed from its side's UPS (ups_a or ups_b). There is
// no rectifier convergence and no PATH_A/PATH_B redundancy at the rack
// level — redundancy lives at the side (block) level. Side-A failure kills
// all 4 of side-A's racks; side-B continues independently.
// States: NORMAL, DEGRADED, FAULT.
#include <winter_river.h>

#ifndef WR_NODE_ID
#error "WR_NODE_ID must be defined via build_flags (e.g. -DWR_NODE_ID=\"server_rack_a1\")"
#endif
#ifndef WR_RACK_LABEL
#error "WR_RACK_LABEL must be defined via build_flags (e.g. -DWR_RACK_LABEL=\"rack_a1\")"
#endif

static const char *NODE_ID = WR_NODE_ID;
static const char *LABEL   = WR_RACK_LABEL;

static constexpr int VOLTAGE_RATING = 48;

static int    cpu_load_pct = 42;
static int    inlet_temp_f = 75;
static int    units_active = 8;
static float  power_kw     = 3.2f;
static String state        = "NORMAL";

static void updateState() {
  if (inlet_temp_f > 95 || cpu_load_pct > 95) {
    state = "FAULT";
  } else if (inlet_temp_f > 85 || cpu_load_pct > 80) {
    state = "DEGRADED";
  } else {
    state = "NORMAL";
  }
}

static void handleToken(const String &tok) {
  if (tok.startsWith("CPU:")) {
    cpu_load_pct = tok.substring(4).toInt();
    power_kw = 1.2f + (cpu_load_pct / 100.0f) * 6.0f;
  } else if (tok.startsWith("TEMP:")) {
    inlet_temp_f = tok.substring(5).toInt();
  } else if (tok.startsWith("UNITS:")) {
    units_active = tok.substring(6).toInt();
  }
}

static void handleStatusToken(const String &tok, bool &status_set) {
  if (tok.startsWith("STATUS:")) {
    state = tok.substring(7);
    status_set = true;
    return;
  }
  handleToken(tok);
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
    if (tok.length() > 0) handleStatusToken(tok, status_set);
    if (sp < 0) break;
    start = sp + 1;
  }

  if (!status_set) updateState();
}

static void renderDisplay() {
  wr::displayHeader(LABEL, state);
  wr::displayNetLine();
  wr::display.print(F("CPU:"));   wr::display.print(cpu_load_pct); wr::display.print(F("% Temp:"));
  wr::display.print(inlet_temp_f); wr::display.println(F("F"));
  wr::display.print(F("Power:")); wr::display.print(power_kw, 1);  wr::display.print(F("kW U:"));
  wr::display.println(units_active);
  wr::displayFooter();
  wr::display.display();
}

void setup() { wr::begin(NODE_ID, onMqtt); }

void loop() {
  if (!wr::mqttReconnect(NODE_ID)) { delay(1000); return; }
  wr::mqtt.loop();  // pump every iteration: drain queued control + service keepalive
  if (!wr::dueForTelemetry()) { delay(10); return; }
  wr::message_count++;
  renderDisplay();

  String payload = String("{\"ts\":\"") + wr::timestamp() +
                   "\",\"cpu_pct\":"   + String(cpu_load_pct) +
                   ",\"inlet_f\":"     + String(inlet_temp_f) +
                   ",\"power_kw\":"    + String(power_kw, 1) +
                   ",\"units\":"       + String(units_active) +
                   ",\"state\":\""     + state + "\"" +
                   ",\"voltage\":"     + String(VOLTAGE_RATING) +
                   "}";
  wr::mqtt.publish(wr::statusTopic(NODE_ID).c_str(), payload.c_str(), true);
  Serial.println(payload);
}
