# I2C Master-Lesearchitektur für Sensor-Stack

## Übersicht

Dieses Dokument beschreibt das Konzept für den I2C-basierten Lesezugriff eines ESP32-P4 Masters auf den Sensor-Stack des ESP32-S3 Slaves.

## Architektur

```
┌─────────────────────────────────────────────────────────────────┐
│                    ESP32-S3 (Slave)                             │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │  LoRa Rx    │  │  ESP-NOW Rx  │  │   sensor_stack_t     │   │
│  │  (SX1262)   │  │  (WiFi)      │  │   64 Slots (0-63)    │   │
│  └──────┬──────┘  └──────┴───────┘  └──────────┬───────────┘   │
│         │                                       │               │
│         └───────────────────────────────────────┤               │
│                                                 ▼               │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              I2C Slave Interface (Hardware)              │  │
│  │  Register-basiert mit FIFO-Leseprinzip                   │  │
│  └──────────────────────────────────────────────────────────┘  │
│                    ▲        │                                    │
│                    │ I2C    │                                   │
└────────────────────┼────────┼──────────────────────────────────┘
                     │        │
              ┌──────┴──────┐ │
              │  SCL GPIO21 │ │
              │  SDA GPIO20 │ │
              │  Addr 0x38  │ │
              └──────┬──────┘ │
                     │        │
                     │        ├──────────────────────────────┐
                     │        │                               │
              ┌──────┴──────┐ │                               │
              │  ESP32-P4   │ │                               │
              │  (Master)   │ │                               │
              │             │ │                               │
              │  liest       │ │                               │
              │  FIFO        │ │                               │
              │  Pakete      │ │                               │
              └─────────────┘ │                               │
                               │
                    ┌──────────┴──────────┐
                    │                     │
              ┌─────┴─────┐       ┌──────┴──────┐
              │ Display   │       │ LoRa/ESP-NOW│
              │ + Button  │       │ Sensoren    │
              └───────────┘       └─────────────┘
```

## Datenstruktur

### sensor_packet_t (max 79 Byte)

| Schicht | Feld | Größe | Beschreibung |
|---------|------|-------|--------------|
| L1 | packet_header_t | 4 Byte | msg_type, sensor_nr, sensor_type, payload_len |
| L2 | link_metadata_t | 11 Byte | msg_source, rssi, snr, timestamp |
| L3 | payload[] | 0-64 Byte | Raw-Sensordaten |

### Sensor Stack

- **MAX_SENSORS**: 64 (erweitert von 16)
- **Pro Slot**: sensor_packet_t (79 Byte max) + bool valid (1 Byte)
- **Gesamtspeicher**: ~64 × 80 Byte ≈ 5,12 KB

## I2C Register Map

```
┌──────────┬───────┬───────┬──────────────────────────────────────────┐
│ Adresse  │ Typ   │ Größe │ Beschreibung                           │
├──────────┼───────┼───────┼────────────────────────────────────────┤
│ 0x00     │ R     │ 1 B   │ Anzahl verfügbarer Pakete (count)      │
│ 0x01     │ R     │ 17-79 │ Nächstes Paket lesen (auto-pop)        │
│ 0x23     │ W     │ 1 B   │ Reset Drop-Counter (schreibe 0x01)     │
│ 0x24     │ R     │ 4 B   │ Statistik: total_received (4 B)        │
│ 0x28     │ R     │ 4 B   │ Statistik: total_overwritten (4 B)     │
└──────────┴───────┴───────┴────────────────────────────────────────┘
```

### Paket-Länge

Das Paket bei Register 0x01 hat variable Länge:
- **Minimal**: 17 Byte (header 4 + metadata 11 + payload 2)
- **Maximal**: 79 Byte (header 4 + metadata 11 + payload 64)
- Die tatsächliche Länge steht im `packet_header.payload_len` Feld

## Abfrage-Protokoll

### Master Schleife

```
Master: READ 0x00 → count (z.B. 3 Pakete verfügbar)

// Schleife: Solange Pakete vorhanden
Master: WRITE 0x01 → (Trigger Read + Pop)
Master: READ  0x01 → Paket (17-79 Byte, variabel je payload_len)
       → Slot wird automatisch vom Slave entfernt

Master: READ 0x00 → count (z.B. 2, noch welche da)
Master: WRITE 0x01
Master: READ  0x01 → Nächstes Paket
...
Master: READ 0x00 → count (0, keine mehr)
```

### Beispiel-Ablauf

```
Master: READ 0x00 → 3 (drei Pakete verfügbar)

// Paket 1 lesen
Master: WRITE 0x01
Master: READ  0x01 → [79 Byte: sensor=1, type=BME280, payload=12]
       → Sensor 1 Daten verarbeitet, Slot gelöscht

// Paket 2 lesen
Master: WRITE 0x01
Master: READ  0x01 → [17 Byte: sensor=3, type=HDC1080, payload=0]
       → Sensor 3 Daten verarbeitet, Slot gelöscht

// Paket 3 lesen
Master: WRITE 0x01
Master: READ  0x01 → [36 Byte: sensor=5, type=RAIN, payload=4]
       → Sensor 5 Daten verarbeitet, Slot gelöscht

// Keine Pakete mehr
Master: READ 0x00 → 0
```

## Datenfluss

```
Master                          Slave (ESP32-S3)
─────────                       ────────────────

  │                               │
  │──── READ 0x00 (count) ──────→│ sensor_stack_count()
  │                               │
  │──── WRITE 0x01 ─────────────→│ sensor_stack_pop()
  │                               │
  │←── READ 0x01 (Packet) ──────│ Return packet or empty
  │                               │
  │──── WRITE 0x01 ─────────────→│ sensor_stack_pop()
  │                               │
  │←── READ 0x01 (Packet) ──────│ Return next packet
  │                               │
  │──── READ 0x00 (count) ──────→│ sensor_stack_count()
  │←── 0 (keine Pakete) ────────│
  │                               │
```

## Warum dieses Design?

| Feature | Beschreibung |
|---------|--------------|
| **Kein Bitmap** | Slave verwaltet FIFO-Queue automatisch |
| **Keine Sensor-Nr** | Master liest einfach das nächste Paket |
| **Transparent** | Paket enthält sensor_nr + sensor_type |
| **Einfach** | Nutzt bestehendes sensor_stack_pop() |
| **FIFO** | Älteste Daten zuerst |

## Konstanten

```c
#define MAX_SENSORS              64   // Erweitert von 16 auf 64
#define I2C_SLAVE_ADDR           0x38
#define I2C_CLOCK_KHZ            400

// I2C Register Map
#define I2C_REG_COUNT            0x00    // Number of available packets (1 byte)
#define I2C_REG_PACKET_READ      0x01    // Read & consume next packet (variable length)
#define I2C_REG_RESET_DROP       0x23    // Write 0x01 to reset dropped counter
#define I2C_REG_STATS_RECV       0x24    // Total packets received (4 bytes, uint32_t)
#define I2C_REG_STATS_OVERWR     0x28    // Total overwritten packets (4 bytes, uint32_t)

// Packet size constants
#define I2C_MIN_PACKET_SIZE      17      // header(4) + metadata(11) + min_payload(2)
#define I2C_MAX_PACKET_SIZE      79      // header(4) + metadata(11) + max_payload(64)
```

## Implementierungsstatus

### ✅ Abgeschlossen
- [x] sensor_stack.h: MAX_SENSORS auf 64 erweitert
- [x] sensor_stack.h: I2C Register-Map definiert
- [x] main.c: I2C Slave Task Struktur
- [x] main.c: Register-Lesehandler (count, packet, stats)
- [x] main.c: Register-Schreibhandler (reset drop)

### ⏳ Ausstehend
- [ ] ESP-IDF I2C Slave Hardware-Treiber implementieren
- [ ] Master-Code auf ESP32-P4 Seite
- [ ] Test mit echtem I2C Master