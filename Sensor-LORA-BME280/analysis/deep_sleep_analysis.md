# Heltec WiFi LoRa 32 V3 Deep Sleep Current Analysis

## Problem
- **Measured current**: ~6 mA during deep sleep
- **Expected current**: ~20 μA during deep sleep
- **Board**: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)

---

## Root Cause Analysis

### 1. CRITICAL: CP2104 USB-UART Chip (Most Likely Primary Cause)

The Heltec V3 board contains a CP2104 USB-to-UART converter chip. This chip:

- Draws **~5-15 mA when USB is connected** — this is almost certainly your 6 mA measurement
- Draws **~1-2 mA quiescent current even when USB is disconnected** (due to internal circuitry)

**The CP2104 is permanently powered from the VBUS/3.3V rail on the V3 board** and cannot be powered down via software.

**Test**: Disconnect USB cable completely and measure current with a separate battery or power supply. If current drops to ~20-100 μA, the CP2104 was the culprit.

---

### 2. CRITICAL: V3 Board-Specific: GPIO 3 (I2C SDA) Internal Pull-up

On the Heltec V3, **GPIO 3 is used as I2C SDA for the display** (SSD1306). The V3 board has:

- A **pull-up resistor on GPIO 3** that remains active during deep sleep
- This pull-up can cause significant current leakage through the display's internal circuitry

**Current code** (line 266 in main.c):
```c
gpio_reset_pin(DISPLAY_SDA);  // GPIO 17 on V4, but V3 uses GPIO 3
gpio_reset_pin(DISPLAY_SCL);  // GPIO 18
```

The `gpio_reset_pin()` call returns the pin to GPIO mode, but the **internal RTC GPIO hold state may not be properly set**, causing the pin to float and draw current.

---

### 3. CRITICAL: GPIO Hold State Not Set Before Power Domain Shutdown

In [`start_deep_sleep()`](main/main.c:251), the order of operations is problematic:

```c
// Current code order:
gpio_set_level(PIN_VEXT,   1);  // Turn off display
gpio_set_level(PIN_PA_CSD, 0);  // GC1109 disabled
gpio_set_level(PIN_PA_CPS, 0);
gpio_set_level(PIN_VFEM,   0);  // FEM power off

gpio_reset_pin(I2C_SDA);
gpio_reset_pin(I2C_SCL);
gpio_reset_pin(DISPLAY_SDA);
gpio_reset_pin(DISPLAY_SCL);
gpio_reset_pin(DISPLAY_RST);

// GPIO hold is called AFTER pins are reset
gpio_deep_sleep_hold_en();  // <-- TOO LATE for some pins

// Power domain config
esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);

esp_deep_sleep_start();
```

**The problem**: `gpio_reset_pin()` is called BEFORE `gpio_deep_sleep_hold_en()`. This means:
- Pins are released from their configured state
- Then `gpio_deep_sleep_hold_en()` tries to hold them
- But the hold may not capture the correct state for all pins

**Correct order**:
1. Set all pins to desired deep-sleep state
2. Call `gpio_deep_sleep_hold_en()` to lock all pin states
3. Configure power domains
4. Enter deep sleep

---

### 4. SX1262 Sleep Mode Configuration

In [`sx1262_sleep()`](components/sx1262/sx1262.c:874):
```c
uint8_t sleep_config = 0x04; // Warm start
```

- `0x04` = Warm start (PLL stays active) → ~2 μA additional current
- `0x00` = Cold start (everything off) → ~0.1 μA additional current, but longer wake time

This is a minor issue but should be fixed for minimum current.

---

### 5. Heltec V3 vs V4 Pin Definition Mismatch

**Important**: Your code comments reference "Heltec V4" but you're using V3. The pin definitions differ:

| Function | V3 Pin | V4 Pin |
|----------|--------|--------|
| VEXT | GPIO 36 | GPIO 36 (same) |
| VFEM | GPIO 7 | GPIO 7 (same) |
| Display SDA | **GPIO 3** | GPIO 17 |
| Display SCL | **GPIO 8** | GPIO 18 |
| Display RST | **GPIO 20** | GPIO 21 |
| LoRa NSS | GPIO 8 | GPIO 8 |
| LoRa MISO | GPIO 3 | GPIO 11 |
| LoRa MOSI | GPIO 5 | GPIO 10 |
| LoRa SCK | GPIO 6 | GPIO 9 |

**If the pin definitions in [`sx1262.h`](components/sx1262/sx1262.h:9) are for V4 but you're using V3**, the SPI pins may not be correctly configured, causing the SX1262 to not properly enter sleep mode.

---

### 6. BME280 I2C Pull-up Current

The I2C bus is initialized with:
```c
.flags.enable_internal_pullup = 1,
```

During deep sleep, these internal pull-ups remain active and can draw current through the BME280's internal circuitry. The BME280 itself draws ~0.1 μA in sleep mode, but the pull-up resistors can add additional current.

---

### 7. Missing: Power Down ADC and Other RTC Peripherals

The ESP32-S3 has several RTC peripherals that may remain active:
- ADC1/ADC2 RTC peripherals
- RTC IO matrix
- RTC timers

These should be explicitly powered down before deep sleep.

---

## Recommended Fix Strategy

### Phase 1: Immediate Fixes (High Impact)

1. **Ensure USB is disconnected during measurement** — The CP2104 is the most likely cause of 6 mA
2. **Fix GPIO hold order** — Set pins → Hold → Power down → Sleep
3. **Use cold start for SX1262** — Change sleep config from `0x04` to `0x00`
4. **Hold specific GPIOs before deep sleep** — Especially VEXT, FEM pins, and LoRa pins

### Phase 2: Board-Specific Fixes (Medium Impact)

5. **Configure V3-specific pins** — Verify pin definitions match V3 hardware
6. **Reset and hold I2C pins** — Put I2C pins in no-pull state before hold
7. **Power down ADC1** — Call `adc_oneshot_del_unit()` and `adc_cali_delete_scheme_curve_fitting()` before sleep

### Phase 3: Advanced Optimization (Low Impact)

8. **Disable RTC peripherals** — Explicitly power down ADC, DAC, RTC IO
9. **Use `esp_sleep_enable_gpio_wakeup()` instead of EXT1** — GPIO wakeup can be more efficient
10. **Check external circuitry** — Verify no external components are drawing current

---

## Code Changes Required

### In [`start_deep_sleep()`](main/main.c:251):

```c
static void start_deep_sleep(void) {
    // 1. Put radio to sleep FIRST
    sx1262_sleep();
    sx1262_deinit_bus();
    
    // 2. Power off all peripherals
    gpio_set_level(PIN_VEXT,   1);  // display off
    gpio_set_level(PIN_PA_CSD, 0);  // GC1109 disabled
    gpio_set_level(PIN_PA_CPS, 0);
    gpio_set_level(PIN_VFEM,   0);  // FEM power off
    
    // 3. Reset pins to known state (input, no pull)
    gpio_config_t no_pull_input = {
        .pin_bit_mask = (1ULL << I2C_SDA | 1ULL << I2C_SCL |
                         1ULL << DISPLAY_SDA | 1ULL << DISPLAY_SCL |
                         1ULL << DISPLAY_RST |
                         1ULL << PIN_VEXT | 1ULL << PIN_VFEM |
                         1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&no_pull_input);
    
    // 4. NOW enable deep sleep hold (AFTER all pins are set)
    gpio_deep_sleep_hold_en();
    
    // 5. Power down VDDSDIO domain
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
    
    // 6. Configure wakeup sources
    esp_sleep_enable_timer_wakeup(1000000ULL * SLEEP_TIME_SECONDS);
    esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_0, ESP_EXT1_WAKEUP_ANY_LOW);
    
    ESP_LOGI(TAG, "Deep sleep for %d s ...", SLEEP_TIME_SECONDS);
    esp_deep_sleep_start();
}
```

### In [`sx1262_sleep()`](components/sx1262/sx1262.c:874):

```c
esp_err_t sx1262_sleep(void) {
    uint8_t sleep_config = 0x00; // Cold start for minimum current
    esp_err_t ret = sx1262_write_command(SX1262_CMD_SET_SLEEP, &sleep_config, 1);
    return ret;
}
```

---

## Measurement Procedure

1. **Disconnect USB cable** completely
2. **Power from battery** or external 3.3V supply
3. **Measure current** with a multimeter or power analyzer in series
4. **Wait 10+ minutes** to ensure deep sleep current is stable
5. **Compare** with expected ~20-100 μA

If current is still high after all fixes:
- Check for external components on GPIO pins
- Verify no solder bridges on the board
- Consider that the Heltec V3 board itself may have inherent leakage due to onboard components (LEDs, voltage regulator feedback, etc.)

---

## Heltec V3 Board Limitations

The Heltec WiFi LoRa 32 V3 has inherent current consumption sources that cannot be eliminated:

| Component | Minimum Current | Controllable |
|-----------|----------------|--------------|
| ESP32-S3 (deep sleep) | ~10 μA | No |
| CP2104 USB-UART | ~1-2 mA | No |
| SX1262 (sleep) | ~0.1 μA | Yes |
| BME280 (sleep) | ~0.1 μA | No |
| OLED (SSD1306) | ~0 μA (powered off) | Yes |
| Board LEDs | ~0-2 mA | Partially |
| Voltage regulator quiescent | ~50-200 μA | No |

**Realistic minimum for V3**: ~1-3 mA (due to CP2104 and board components)

For true ~20 μA deep sleep, you would need:
- A board without CP2104 (e.g., Heltec V4 with ESP32-S3 but no USB-UART, or a custom board)
- Or disconnect the CP2104 by cutting a trace on the V3 board
