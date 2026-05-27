// bms.cpp — Building Management System aggregator (firmware-side renderer).
//
// The BMS is broker-driven: broker/main.py reads every winter-river/+/status
// topic, computes a rolled-up operational health JSON, and publishes it
// retained to winter-river/bms/status. This firmware:
//   1. Connects to MQTT with standard wr boot.
//   2. Additionally subscribes to its OWN /status topic to receive the
//      broker's aggregate (in addition to /control for manual overrides).
//   3. Parses incoming JSON fields and renders them on the OLED.
//   4. Does NOT publish telemetry on every tick (broker owns /status).
//      Only the initial ONLINE override from wr::mqttReconnect is published,
//      and the broker's next aggregate overwrites it ~1 second later.
//
// States (echoed from broker aggregate): NORMAL, DEGRADED, ALARM, FAULT
#include <winter_river.h>

static const char *NODE_ID = "bms";
static const char *LABEL   = "bms";

// Rolled-up state from broker's bms/status aggregate. Defaults shown until
// the first aggregate arrives.
static String mode             = "BOOT";
static String power_state      = "UNKNOWN";
static String rack_a_state     = "?";
static String rack_b_state     = "?";
static String side_a_health    = "?";
static String side_b_health    = "?";
static int    active_alarms    = 0;
static int    degraded_paths   = 0;
static int    online_nodes     = 0;
static int    offline_nodes    = 0;
static float  pue              = 0.0f;
static bool   subscribed_status = false;

// ─── Tiny JSON value extractor (string + number, no nesting) ────────────────
// We only need to read top-level scalar fields from the broker's flat aggregate.
// Returns the raw value string (without quotes) or empty if not found.
static String jsonField(const String &doc, const String &key) {
  String needle = "\"" + key + "\":";
  int idx = doc.indexOf(needle);
  if (idx < 0) return "";
  idx += needle.length();
  while (idx < (int)doc.length() && doc[idx] == ' ') idx++;
  if (idx >= (int)doc.length()) return "";
  if (doc[idx] == '"') {
    int end = doc.indexOf('"', idx + 1);
    return (end < 0) ? "" : doc.substring(idx + 1, end);
  }
  int end = idx;
  while (end < (int)doc.length()
         && doc[end] != ',' && doc[end] != '}' && doc[end] != ' ') end++;
  return doc.substring(idx, end);
}

// ─── Token handler for /control commands (manual overrides only) ────────────
static void handleControlToken(const String &tok) {
  if (tok.startsWith("STATUS:"))   mode = tok.substring(7);
  // ACK and SILENCE are documented but operate at the broker layer; the
  // firmware just echoes the command back to the user via OLED for now.
}

// ─── Parse broker aggregate JSON and refresh display state ──────────────────
static void parseAggregate(const String &doc) {
  String v;

  v = jsonField(doc, "mode");           if (v.length()) mode          = v;
  v = jsonField(doc, "power_state");    if (v.length()) power_state   = v;
  v = jsonField(doc, "rack_a_state");   if (v.length()) rack_a_state  = v;
  v = jsonField(doc, "rack_b_state");   if (v.length()) rack_b_state  = v;
  v = jsonField(doc, "side_a_health");  if (v.length()) side_a_health = v;
  v = jsonField(doc, "side_b_health");  if (v.length()) side_b_health = v;
  v = jsonField(doc, "active_alarms");  if (v.length()) active_alarms  = v.toInt();
  v = jsonField(doc, "degraded_paths"); if (v.length()) degraded_paths = v.toInt();
  v = jsonField(doc, "online_nodes");   if (v.length()) online_nodes   = v.toInt();
  v = jsonField(doc, "offline_nodes");  if (v.length()) offline_nodes  = v.toInt();
  v = jsonField(doc, "pue");            if (v.length()) pue            = v.toFloat();
}

// ─── Single MQTT callback for BOTH /control and /status ─────────────────────
static void onMqtt(char *topic, byte *p, unsigned int l) {
  String t(topic);
  String payload;
  payload.reserve(l);
  for (unsigned int i = 0; i < l; i++) payload += (char)p[i];

  if (t.endsWith("/status")) {
    // Broker aggregate — ignore our own ONLINE-heartbeat echo (no "mode" field).
    if (payload.indexOf("\"mode\":") >= 0) parseAggregate(payload);
  } else if (t.endsWith("/control")) {
    int start = 0;
    while (start <= (int)payload.length()) {
      int sp = payload.indexOf(' ', start);
      String tok = (sp < 0) ? payload.substring(start) : payload.substring(start, sp);
      if (tok.length() > 0) handleControlToken(tok);
      if (sp < 0) break;
      start = sp + 1;
    }
  }
}

static void renderDisplay() {
  wr::displayHeader(LABEL, mode);
  wr::displayNetLine();
  wr::display.print(F("PWR:"));  wr::display.print(power_state);
  wr::display.print(F(" PUE:")); wr::display.println(pue, 2);
  wr::display.print(F("Ra:"));   wr::display.print(rack_a_state);
  wr::display.print(F(" Rb:"));  wr::display.println(rack_b_state);
  wr::display.print(F("Alm:"));  wr::display.print(active_alarms);
  wr::display.print(F(" On:"));  wr::display.print(online_nodes);
  wr::display.print(F("/Off:")); wr::display.println(offline_nodes);
  wr::displayFooter();
  wr::display.display();
}

void setup() { wr::begin(NODE_ID, onMqtt); }

void loop() {
  if (!wr::mqttReconnect(NODE_ID)) {
    subscribed_status = false;
    delay(2000);
    return;
  }
  // wr::mqttReconnect already subscribed to /control. After (re)connect,
  // also subscribe to /status so we receive the broker's aggregate.
  if (!subscribed_status) {
    wr::mqtt.subscribe(wr::statusTopic(NODE_ID).c_str(), 1);
    subscribed_status = true;
  }

  wr::message_count++;
  renderDisplay();
  wr::mqtt.loop();

  // NOTE: no telemetry publish here. The broker owns winter-river/bms/status
  // and republishes the full aggregate every tick. The initial ONLINE override
  // from wr::mqttReconnect is overwritten on the broker's next tick.
  delay(wr::TELEMETRY_INTERVAL_MS);
}
