# PDU A

`pdu_a` is the active Side A Power Distribution Unit in the Winter River 25-node topology. It uses the SSD1306 + MQTT/LWT + shared `winter_river` helper pattern used across the active node fleet.

## Current Behavior

- Connects to `WinterRiver-AP`
- Publishes retained JSON telemetry every 5 seconds on `winter-river/pdu_a/status`
- Subscribes to `winter-river/pdu_a/control`
- Uses SSD1306 OLED output via the shared `wr::begin()` initialization path
- Reports `input_v`, `output_v`, `load_pct`, `state`, and `voltage`

## Source of Truth

For the active protocol, thresholds, build commands, and test examples, use the shared PDU documentation:

- [../README.md](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/pdu/README.md)
- [pdu_a.cpp](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/pdu/pdu_a/pdu_a.cpp)

## Historical Note

Older notes may describe an LCD-based PDU prototype that published simple heartbeat strings. That is no longer the active firmware and should not be used as a template.
