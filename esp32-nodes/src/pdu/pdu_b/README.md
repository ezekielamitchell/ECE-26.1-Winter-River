# PDU B *(deprecated — removed from topology)*

`pdu_b` was the Side B Power Distribution Unit in earlier Winter River
revisions. The 2N redesign routes `ups_b` directly into the shared
`rectifier`, so `pdu_b` is no longer present in `scripts/init_db.sql` and the
broker no longer dispatches a `PDU` handler. This firmware folder is kept for
historical reference only.

## Current Behavior

- Connects to `WinterRiver-AP`
- Publishes retained JSON telemetry every 5 seconds on `winter-river/pdu_b/status`
- Subscribes to `winter-river/pdu_b/control`
- Uses SSD1306 OLED output via the shared `wr::begin()` initialization path
- Reports `input_v`, `output_v`, `load_pct`, `state`, and `voltage`

## Source of Truth

For the active protocol, thresholds, build commands, and test examples, use the shared PDU documentation:

- [../README.md](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/pdu/README.md)
- [pdu_b.cpp](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/pdu/pdu_b/pdu_b.cpp)

## Historical Note

Older notes may describe `pdu_b` as an ADC/LCD prototype that published simple heartbeat strings. That is no longer the active firmware and should not be used as a template.
