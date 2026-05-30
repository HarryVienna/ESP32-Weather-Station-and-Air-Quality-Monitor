# Waveshare ESP32-P4-Module-DEV-KIT-C Demo

This repository contains a demonstration project for the **Waveshare ESP32-P4-Module-DEV-KIT-C**, showcasing how to implement a high-performance GUI using the **ESP-IDF** and **LVGL**.

The project features a complete initialization of the **10.1-inch MIPI-DSI display (JD9365)** and **GT911 touch controller** using pure Espressif components and the latest DSI stack.

## 🚀 Key Features

* **MIPI-DSI Integration**: Full initialization of the JD9365 controller using 2-lane DSI.
* **LVGL Integration**: High-performance rendering with double buffering in PSRAM.
* **Software Rotation**: Configured for 1280x800 Landscape mode via LVGL software rotation.
* **Touch Support**: GT911 I2C touch controller integration with coordinate transformation.

## 📖 Detailed Guide

For a comprehensive breakdown of the hardware, the paged register model of the JD9365, and a step-by-step guide on the software setup, please read the full article:

👉 **[Review & Guide: Waveshare ESP32-P4 with 10.1" DSI Display](https://www.haraldkreuzer.net/en/news/waveshare-esp32-p4-module-dev-kit-c-compact-development-board-101-inch-dsi-display)**

## 🛠️ Requirements

* **Hardware**: Waveshare ESP32-P4-Module-DEV-KIT-C (10.1" version).
* **Framework**: ESP-IDF v5.5 or later.

## 🔧 Quick Start

1.  Clone this repository.
2.  Set the target to ESP32-P4: `idf.py set-target esp32p4`.
3.  Build and flash: `idf.py build flash monitor`.

## 📝 License
This project is licensed under the MIT License - see the LICENSE file for details.
