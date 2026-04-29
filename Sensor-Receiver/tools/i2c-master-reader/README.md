# ESP32-P4 I2C Master Reader

Minimaler ESP-IDF Projekt für den ESP32-P4 als I2C-Master zum Auslesen vom ESP32-S3 Slave.

## Hardware-Verbindung

| ESP32-P4 | ESP32-S3 Slave | Beschreibung |
|----------|----------------|--------------|
| GPIO_xx  | SDA (GPIO_47)  | I2C Daten    |
| GPIO_xx  | SCL (GPIO_48)  | I2C Clock    |
| GND      | GND            | Gemeinsame Masse |

**Wichtig:** Gemeinsame Masse (GND) ist erforderlich!

## I2C Register

| Register | Typ  | Länge | Beschreibung |
|----------|------|-------|--------------|
| 0x00     | READ | 1 Byte | Anzahl verfügbarer Pakete |
| 0x01     | R/W  | var. | Nächstes Packet (liest + entfernt vom Stack) |
| 0x24     | READ | 4 Byte | Total received (uint32_t LE) |
| 0x28     | READ | 4 Byte | Total overwritten (uint32_t LE) |

## Build & Flash

```bash
# In ESP-IDF v5.5 Umgebung
cd tools/i2c-master-reader
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/serialport/by-id/<P4-PORT> flash
```

## Ausgabe

```
I (0) CPU: ESP32-P4
I (0) I2C_MASTER: ========================================
I (0) I2C_MASTER:    ESP32-P4 I2C Master Reader
I (0) I2C_MASTER:    Slave: 0x38
I (0) I2C_MASTER: ========================================
I (0) I2C_MASTER: I2C master initialized (SDA=20, SCL=21, freq=400000 Hz)
I (2002) I2C_MASTER: === Tick 1 === Total received: 5
I (2002) I2C_MASTER:   Total overwritten: 0
I (2002) I2C_MASTER:   Available packets: 5
I (2003) I2C_MASTER:   Packet 1 (len=17):
I (2003) I2C_MASTER:     --- Packet Info ---
I (2003) I2C_MASTER:     Sensor : #1
I (2003) I2C_MASTER:     Type   : BME280
I (2003) I2C_MASTER:     Payload: 0 Bytes
I (2003) I2C_MASTER:     Time   : 123456789
I (2003) I2C_MASTER:     RSSI   : -45 dBm
I (2003) I2C_MASTER:     SNR    : 9.5
```

## Dateistruktur

```
tools/i2c-master-reader/
├── CMakeLists.txt          # Top-level CMake
├── sdkconfig               # ESP32-P4 Konfiguration
├── README.md
└── main/
    ├── CMakeLists.txt      # Component CMake
    └── main.c              # I2C Master Reader Code