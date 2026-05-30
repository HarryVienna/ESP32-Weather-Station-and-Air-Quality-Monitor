# Cline Rules for Sensor-Receiver Project

## Project Overview
ESP32-S3 receiver for LoRa and ESP-NOW sensor data. Data is read via I2C by a host controller.

## Build
```bash
# From project root
idf.py build
idf.py flash
idf.py monitor
```

## Build Commands (Cline)

When building the ESP32 project, use these commands:

```bash
# Activate ESP-IDF environment first
call C:\esp\v5.5.4\esp-idf\export.bat

# Then build
idf.py build

# Flash to device (if needed)
idf.py -p COMx flash

# Monitor serial output
idf.py monitor
```

**Important for Cline:**
- Always run `idf.py build` from the project root: `c:\Users\Harald\Documents\Projekte\Wetterstation-ESP32-3.0\Code\Sensor-Receiver`
- Build output is redirected to `build_output.txt` for error checking
- After code changes, always rebuild and check for errors before reporting success
- Use `cmd /c` prefix for IDF commands on Windows (the export.bat requires cmd shell)

## Key Architecture
- **LoRa (SX1262)**: Async receiver with callback → `sensor_stack_push()` → `display_update_async()`
- **ESP-NOW**: Async receiver with callback → `sensor_stack_push()` → `display_update_async()`
- **Display**: Async queue-based update (non-blocking) → `display_task()` runs at low priority
- **I2C Slave**: S3 slave exposes packets to host via I2C register map

## Critical Files
| File | Purpose |
|------|---------|
| `main/main.c` | Entry point, task creation |
| `main/sensor_stack.c/h` | FIFO stack for sensor packets |
| `main/display/display.c` | Async display update via queue |
| `main/network/lora.c` | LoRa receiver callback |
| `main/network/esp-now.c` | ESP-NOW receiver callback |
| `main/i2c/i2c_slave.c` | I2C slave implementation |
| `common/packet_format.h` | Shared packet structures |

## Coding Rules
- **Never block sensor callbacks** — always use async queue for display
- **Use `display_update_async()`** not `display_update()` in LoRa/ESP-NOW callbacks
- **Error handling**: Check return values from ESP functions
- **Thread safety**: Use mutex for shared state, queues for IPC
- **Log levels**: ESP_LOGI for important events, ESP_LOGW for recoverable issues, ESP_LOGE for errors

## Memory Constraints
- ESP32-S3 with ~500KB SRAM
- Keep stack tasks reasonable (2048 bytes minimum for new tasks)
- Display queue max 8 entries — drops updates if full (log at DEBUG level)

## I2C Protocol
- Slave address: 0x38
- SCL speed: 50kHz
- Registers: I2C_REG_COUNT, I2C_REG_PACKET_READ, I2C_REG_SET_TIME, etc.

## Dependencies
- ESP-IDF v5.5.4
- u8g2 (SSD1306 OLED)
- sx1262 driver
- button component

## Task Priorities
- LoRa RX: Higher priority (ISR context)
- ESP-NOW RX: Higher priority
- sensor_stack: Higher priority
- Display task: `tskIDLE_PRIORITY + 1` (lowest)
- main loop: Default