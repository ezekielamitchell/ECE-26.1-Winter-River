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

## 16x2 I2C LCD Not Displaying

**Problem:** LCD backlight is on but no text appears.

**Cause:** Wrong library or wrong I2C address.

**Solution:**
1. Use `LiquidCrystal_I2C` library (not Adafruit SSD1306 which is for OLEDs)
2. Try I2C address `0x27` or `0x3F` - these are the most common
3. Run an I2C scanner to find the correct address

**platformio.ini:**
```ini
lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
```

---
