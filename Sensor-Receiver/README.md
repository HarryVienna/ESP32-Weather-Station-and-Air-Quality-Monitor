# LoRa Test-Tool for Heltec WiFi LoRa 32(V3 & V4), ESP32-S3 + SX1262

**Interactive LoRa test tool with OLED display and real-time parameter configuration**



You can find a detailed introduction to LoRa [on my website](https://www.haraldkreuzer.net/en/news/lora-radio-technology-internet-things) 

Infos about the SX1262, the ESP32 driver and this tool you can find [here](https://www.haraldkreuzer.net/en/news/LoRa-with-the-ESP32-and-Semtech-SX1262) 



![PXL_20251110_092105251_github](https://github.com/user-attachments/assets/1a1c75ff-7fc7-40b1-8ddd-05989fd5cc19)

![ESP32](https://img.shields.io/badge/ESP32-S3-blue) ![LoRa](https://img.shields.io/badge/LoRa-SX1262-green) 

---

## Features

**Interactive menu** on 128x64 OLED display  
**Intuitive navigation** with short-click and long-press  
**Live configuration** of all LoRa parameters without restart  
**Send mode** with packet counter and transmit indicator  
**Receive mode** with RSSI display and packet statistics  
**Real-time updates** when changing parameters  


## Hardware

**Board**: Heltec WiFi LoRa 32 V3.x or V4
**Display**: 128x64 OLED (SSD1306)
**LoRa**: SX1262 Transceiver
**Button**: GPIO 0 (Boot button)

⚠️ **Important:** Always connect antenna!

## Display Layout

```
┌────────────────────────────┐
│ >Mode: Send           [*]  │ ← Active menu item (>)
│  SF: 7                     │   Edit mode ([inverted])
│  BW: 125                   │
│  CR: 4/5                   │
│  Pwr: 14                   │
│ ────────────────────────── │
│ TX: 42                   * │ ← Status: Packets + transmit indicator
└────────────────────────────┘

In receive mode:
┌────────────────────────────┐
│  Mode: Recv                │
│  SF: 7                     │
│  BW: 125                   │
│  CR: 4/5                   │
│  Pwr: 14                   │
│ ────────────────────────── │
│ RX:15 RSSI:-85             │ ← Received packets + RSSI
└────────────────────────────┘
```

## Navigation

### Left Side (Menu Navigation)

**Short Click (< 500ms)**
- Jumps to next menu item
- Cyclic: Mode → SF → BW → CR → Power → Mode

**Long Press (≥ 500ms)**
- Switches to edit mode (right side)
- Current value is displayed inverted

### Right Side (Value Editing)

**Short Click (< 500ms)**
- Changes to next value (cycles through options)
- Updates LoRa configuration **immediately**

**Long Press (≥ 500ms)**
- Exits edit mode
- Returns to menu navigation (left side)

## Parameter Ranges

| Parameter | Values | Description |
|-----------|--------|-------------|
| **Mode** | Send / Receive | Operating mode |
| **SF** | 5 - 12 | Spreading Factor (cyclic) |
| **BW** | 125 / 250 / 500 | Bandwidth in kHz (cyclic) |
| **CR** | 4/5, 4/6, 4/7, 4/8 | Coding Rate (cyclic) |
| **TX Power** | -9 to +22 | Transmit power in dBm (cyclic) |

## Operating Modes

### Send Mode
- Transmits a packet every 2 seconds
- Packet content: `"PKT:42 SF:7 BW:125"` (with counter)
- Display shows:
  - `TX: 42` → Number of transmitted packets
  - `*` → Blinks when transmitting

### Receive Mode
- Continuously listens for LoRa packets
- Display shows:
  - `RX:15` → Number of received packets
  - `RSSI:-85` → Signal strength of last packet
  - `Waiting...` → When nothing received yet

## Project Structure

```
lora-testtool/
├── main/
│   ├── main.c              # Main program
│   ├── lora_testtool.c     # Test tool logic
│   └── lora_testtool.h
├── components/
│   ├── button/             # Button handler
│   │   ├── button.c
│   │   └── button.h
│   └── sx1262/             # LoRa driver
│       ├── sx1262.c
│       └── sx1262.h
└── CMakeLists.txt
```

### Dependencies

The project requires:
- ✅ **button** - Button library (already included)
- ✅ **sx1262** - Your LoRa driver (already included)
- 📦 **u8g2** - Display library ([u8g2](https://github.com/olikraus/u8g2))
- 📦 **u8g2_hal_esp32** - Display library ESP32 HAL ([u8g2_hal_esp32](https://github.com/mkfrey/u8g2-hal-esp-idf))

## Technical Details

### Task Architecture

The tool creates 3 tasks:

1. **Display Update Task** (Priority 6)
   - Updates display when needed
   - Stack: 3 KB

2. **LoRa Send Task** (Priority 5)
   - Transmits packets in send mode
   - Interval: 2 seconds
   - Stack: 4 KB

3. **LoRa Receive Task** (Priority 6)
   - Receives packets in receive mode
   - Non-blocking with 100ms timeout
   - Stack: 4 KB

## Customization

### Change Transmit Interval

In `lora_testtool.c`:
```c
#define SEND_INTERVAL_MS    2000  // Change to desired value
```

### Change Frequency

In `update_lora_config()`:
```c
.frequency = 868000000,  // Change to your ISM band frequency
```

### Add More Parameters

1. Extend enum `menu_item_t`
2. Extend state `menu_state_t`
3. Add `draw_menu_item()` call in `update_display()`
4. Add case in `menu_next_value()`
5. Adapt `update_lora_config()`

## Timing Diagram

### Send Mode Flow

```
Time: 0s      2s      4s      6s
      ↓       ↓       ↓       ↓
Send: ████    ████    ████    ████
      ↑       ↑       ↑       ↑
      PKT:1   PKT:2   PKT:3   PKT:4

Display:
      TX: 1   TX: 2   TX: 3   TX: 4
      *       *       *       *
```

### Receive Mode Flow

```
Time: 0s         RX        RX           RX
      ↓          ↓         ↓            ↓
Recv: ─────────███───────███─────────███
      ↑         ↑         ↑            ↑
      Waiting   RSSI:-85  RSSI:-82     RSSI:-90

Display:
      Waiting   RX:1      RX:2         RX:3
                RSSI:-85  RSSI:-82     RSSI:-90
```

## Links

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [SX1262 Datasheet](https://www.semtech.com/products/wireless-rf/lora-core/sx1262)
- [LoRa Basics](https://www.semtech.com/lora/resources/lora-community/)
