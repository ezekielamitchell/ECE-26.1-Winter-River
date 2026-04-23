# UPS A

`ups_a` is the active Side A Uninterruptible Power Supply in the Winter River 25-node topology. It uses the same SSD1306 + MQTT/LWT + shared `winter_river` helper pattern as `ups_b`.

## Current Behavior

- Connects to `WinterRiver-AP`
- Publishes retained JSON telemetry every 5 seconds on `winter-river/ups_a/status`
- Subscribes to `winter-river/ups_a/control`
- Uses SSD1306 OLED output via the shared `wr::begin()` initialization path
- Reports `battery_pct`, `load_pct`, `input_v`, `output_v`, `state`, and `voltage`

## Source of Truth

For the active protocol, thresholds, build commands, and test examples, use the shared UPS documentation:

- [../README.md](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/ups/README.md)
- [ups_a.cpp](/Users/house/Developer/ECE-26.1-Winter-River/esp32-nodes/src/ups/ups_a/ups_a.cpp)

## Historical Note

Older one-off notes may still describe an LCD-era prototype or an outdated topology position for `ups_a`. Use the shared UPS documentation and current source as the truth.
