# ESP32 Weather Station, Indoor Air Quality & Radiation Monitor

A DIY weather station built from several cooperating ESP32/CubeCell devices: a touchscreen base
station that displays everything, a dedicated radio gateway, and a family of battery-powered
wireless sensor nodes (temperature/humidity, barometric pressure, and radiation) spread around a
house and garden.

<!--
  TODO: add a photo of the assembled hardware (base station + a few sensor nodes).
  Place the file at e.g. docs/images/hardware.jpg.
-->
![The weather station hardware](docs/images/hardware.jpg)
*The base station together with some of the wireless sensor nodes.*

## Overview

The system is split into three tiers:

1. **[`BaseStation/`](BaseStation/README.md)** — an ESP32-P4 with a 10.1" DSI touchscreen. The only
   part you actually interact with: it shows live sensor readings, 24h history charts, a weather
   forecast pulled from the internet, and lets you configure WiFi/location/sensor icons entirely
   from the touchscreen. It also carries its own built-in air quality sensor (SEN66).
2. **[`Sensor-Receiver/`](Sensor-Receiver/)** — an ESP32-S3 that acts as a radio gateway. It listens
   for both LoRa and ESP-NOW packets from the wireless sensor nodes, buffers them, and exposes them
   to the base station over an I2C slave interface. It has its own small OLED status display.
3. **Wireless sensor nodes** — several independent, battery-powered firmwares (see the table below),
   each reading one physical sensor and periodically transmitting a small packet over LoRa or
   ESP-NOW before going back to sleep.

```
 Wireless sensor nodes  --LoRa/ESP-NOW-->  Sensor-Receiver  --I2C-->  BaseStation  <--HTTPS-->  Weather API
 (battery, deep sleep)                     (ESP32-S3)                (ESP32-P4 +
                                                                       touchscreen)
```

## Components

| Folder | Board | Framework | Sensor | Radio | Role |
|---|---|---|---|---|---|
| [`BaseStation`](BaseStation/README.md) | Waveshare ESP32-P4-Module-DEV-KIT | ESP-IDF | SEN66 (built-in) | — | Display, GUI, weather fetch |
| [`Sensor-Receiver`](Sensor-Receiver/) | Heltec WiFi LoRa 32 (ESP32-S3) | ESP-IDF | — | LoRa + ESP-NOW (receiver) | Radio gateway to BaseStation (I2C) |
| [`Sensor-LORA-BME280`](Sensor-LORA-BME280/) | Heltec WiFi LoRa 32 V3/V4 (ESP32-S3) | ESP-IDF | BME280 | LoRa | Temperature/humidity/pressure node |
| [`Sensor-LORA-BME280-HTCC-AB01`](Sensor-LORA-BME280-HTCC-AB01/) | Heltec CubeCell HTCC-AB01 V2 | PlatformIO/Arduino | BME280 | LoRa | Same as above, tiny low-power board |
| [`Sensor-LORA-SHT45`](Sensor-LORA-SHT45/) | Heltec WiFi LoRa 32 V3/V4 (ESP32-S3) | ESP-IDF | SHT45 | LoRa | Temperature/humidity node (no pressure) |
| [`Sensor-LORA-RadiationD`](Sensor-LORA-RadiationD/) | Heltec WiFi LoRa 32 V4 (ESP32-S3) | ESP-IDF | RadiationD-v1.1 Geiger tube | LoRa | Geiger counter node, ULP pulse counting |
| [`Sensor-LORA-RadiationD-HTCC-AB01`](Sensor-LORA-RadiationD-HTCC-AB01/) | Heltec CubeCell HTCC-AB01 V2 | PlatformIO/Arduino | RadiationD-v1.1 Geiger tube | LoRa | Same as above, tiny low-power board |
| [`Sensor-ESP-NOW-BME280`](Sensor-ESP-NOW-BME280/) | generic ESP32 | ESP-IDF | BME280 | ESP-NOW | Temperature/humidity/pressure node (the original, oldest project here) |
| [`EEZ-Studio-1280x800`](EEZ-Studio-1280x800/) | — | [EEZ Studio](https://github.com/eez-open/studio) project | — | — | Design-time source for BaseStation's LVGL touchscreen GUI |

Every LoRa node uses the same link parameters (869.525 MHz, BW125, CR 4/5, sync word `0x1424`), so
the ESP32-based and CubeCell-based variants of the same sensor are interchangeable from
`Sensor-Receiver`'s point of view — it doesn't know or care which MCU sent a packet.

## Shared wire protocol

All sensor nodes and `Sensor-Receiver` share a single `common/packet_format.h` (copied into each
firmware, byte-for-byte identical) that defines the over-the-air packet layout:

- **`message_type_t`** — `PAIRING_REQ` (0), `PAIRING_RESP` (1), `DATA` (2). Pairing is only used on
  the ESP-NOW side, to let a sensor learn the receiver's MAC address/channel on first boot (and
  re-pair automatically if a send ever fails).
- **`sensor_type_t`** — `SENSOR_TYPE_BME280` (1), `SENSOR_TYPE_SHT45` (2), `SENSOR_TYPE_GEIGER` (3),
  `SENSOR_TYPE_CUSTOM` (255).
- **`packet_header_t`** (4 bytes, packed) — `msg_type`, `sensor_nr` (which of the 6 slots this
  sensor occupies), `sensor_type`, `payload_len`.
- **`link_metadata_t`** (11 bytes) — `msg_source` (LoRa/ESP-NOW), `rssi`, `snr`, `timestamp`. Added
  by `Sensor-Receiver` itself; never transmitted over the air.
- One payload struct per sensor type (`bme280_payload_t`, `sht45_payload_t`, `geiger_payload_t`),
  each carrying the measurement plus the sending node's battery voltage.

`BaseStation` reads the same header to know which of its 6 sensor-card slots a packet belongs to
and which struct to cast the payload to.

## Power management

Every wireless node spends nearly all of its life in deep sleep and wakes only to measure and
transmit:

- Temperature/humidity/pressure nodes wake every **10 minutes**, take one reading, transmit, and go
  back to sleep.
- Geiger counter nodes need to accumulate pulses over time rather than take a single instant
  reading, so they wake more often (every 60s on the ESP32 variant, with a rolling average kept in
  RTC memory across wakeups) and keep counting through sleep: the ESP32-based sensor runs a small
  ULP (co-processor) program that counts Geiger pulses even while the main CPU is powered down, so
  no pulses are lost between wakeups. It also uses dynamic CPU frequency scaling and gates the LoRa
  power amplifier on only for the moment of transmission — an iterative optimization that brought
  its average current down from roughly 45 mA to under 30 mA.
- Before sleeping, every ESP32-based node explicitly isolates the radio's SPI pins, the OLED's power
  rail, and unused UART pins to avoid leakage current through them.
- The CubeCell-based nodes (`*-HTCC-AB01`) use the ASR6502's native low-power timer/interrupt
  wake instead of ESP32 deep sleep, since that board has no equivalent RTC/ULP domain.

## Getting started

Each folder is an independent firmware project — `BaseStation`, `Sensor-Receiver` and the ESP32-based
sensor nodes are ESP-IDF projects (`idf.py build`/`flash`); the two `*-HTCC-AB01` nodes are
PlatformIO/Arduino projects (`pio run --target upload`). See
**[`BaseStation/README.md`](BaseStation/README.md)** for the full build/flash walkthrough of the
main display unit, which is the best starting point if you just want to see the system running.

## License

[GPL-3.0](LICENSE)

## Acknowledgments

- [EEZ Studio](https://github.com/eez-open/studio) for the GUI editor/code generator
- [LVGL](https://lvgl.io/) as the graphics library
- Espressif for ESP-IDF and the `esp_lvgl_port`/`esp_lcd_*` driver components
- Heltec for the WiFi LoRa 32 and CubeCell hardware/BSPs
