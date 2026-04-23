# Troubleshooting Guide

## ESP32 ADC Noise When WiFi Active

**Problem:** ADC readings fluctuate significantly (e.g., 2.3-2.6V when expecting 3.3V) when WiFi is enabled.

**Cause:** The ESP32's WiFi radio causes interference with ADC readings. This is a known hardware limitation.

**Solution - Software Averaging:**
```cpp
// Average multiple readings to reduce noise
long sum = 0;
for (int i = 0; i < 64; i++) {
  sum += analogRead(ADC_PIN);
}
int esp_raw_value = sum / 64;
double voltage = esp_raw_value * (3.3 / 4095.0);
```

**Additional Fixes:**
- Add a 0.1µF capacitor between the ADC pin and GND for hardware filtering
- Use ADC1 pins only (GPIO 32-39) - ADC2 pins do not work when WiFi is active
- ESP32 ADC is inherently ~1-2% inaccurate even with averaging

---

## SSD1306 OLED Not Displaying

**Problem:** The OLED stays blank, shows only noise, or never gets past the boot screen.

**Cause:** Most active nodes now use the shared `winter_river` helper, which initializes an SSD1306 OLED and probes I2C addresses `0x3C` then `0x3D`. Blank output usually means the module is not ACKing on either address, wiring is wrong, or the wrong display library was installed.

**Solution:**
1. Confirm the node is using `Adafruit SSD1306` and `Adafruit GFX` rather than `LiquidCrystal_I2C`
2. Check SDA/SCL wiring and power before debugging firmware
3. Watch serial boot output for the detected OLED address from `wr::begin()`
4. If needed, run a simple I2C scanner sketch to verify whether the panel responds at `0x3C` or `0x3D`

**platformio.ini:**
```ini
lib_deps =
    adafruit/Adafruit SSD1306@^2.5.7
    adafruit/Adafruit GFX Library@^1.11.5
```

**Historical note:** Older prototype docs may mention `LiquidCrystal_I2C` LCD modules. Those notes do not apply to the active 25-node SSD1306 firmware set.

---
