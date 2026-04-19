# Network Structure Refactoring Plan

## Ziel
Vereinheitlichung der Netzwerkmodule (LoRa und ESP-NOW) in einem gemeinsamen Ordner `network/`.

## Aktuelle Struktur
```
main/
├── main.c                          # init_lora() hier (Zeilen 121-140)
├── esp-now/
│   ├── network.c                   # ESP-NOW Funktionen
│   └── network.h
└── lora/
    ├── lora_receiver.c             # LoRa Receiver
    └── lora_receiver.h
```

## Neue Struktur
```
main/
├── main.c                          # init_lora() entfernt
└── network/
    ├── esp-now.c                   # ESP-NOW Funktionen (von esp-now/network.c)
    ├── esp-now.h                   # ESP-NOW Header (von esp-now/network.h)
    ├── lora.c                      # LoRa Receiver + init_lora()
    └── lora.h                      # LoRa Header (von lora_receiver.h)
```

## Änderungen im Detail

### 1. Datei-Umbenennungen und Verschiebungen

| Von | Nach |
|-----|------|
| `main/esp-now/network.c` | `main/network/esp-now.c` |
| `main/esp-now/network.h` | `main/network/esp-now.h` |
| `main/lora/lora_receiver.c` | `main/network/lora.c` |
| `main/lora/lora_receiver.h` | `main/network/lora.h` |

### 2. main/main.c Änderungen

- **Entfernen:** `init_lora()` Funktion (Zeilen 121-140)
- **Ändern:** `#include "lora/lora_receiver.h"` → `#include "network/lora.h"`
- **Ändern:** `#include "esp-now/network.h"` → `#include "network/esp-now.h"`
- **Beibehalten:** `lora_receiver_start()` Aufruf (wird zu `lora_start()`)
- **Entfernen:** `lora_receiver_init()` Aufruf (wird gelöscht)

### 3. network/lora.c Änderungen

- **Hinzufügen:** `init_lora()` Funktion von main.c
- **Entfernen:** `lora_receiver_init()` Funktion (g_packets_received, g_crc_errors nicht benötigt)
- **Umbenennen:** `lora_receiver_start()` → `lora_start()`
- **Umbenennen:** `lora_receiver_stop()` → `lora_stop()` (falls vorhanden)
- **Entfernen:** `lora_receiver_stats()` Funktion (nicht mehr benötigt)
- **Globale Variablen entfernen:** `g_packets_received`, `g_crc_errors`

### 4. network/lora.h Änderungen

- **Umbenennen:** `lora_receiver_init()` → entfernen
- **Umbenennen:** `lora_receiver_start()` → `lora_start()`
- **Umbenennen:** `lora_receiver_stop()` → `lora_stop()`
- **Umbenennen:** `lora_receiver_stats()` → entfernen
- **Neu:** `init_lora()` Funktion deklarieren

### 5. network/esp-now.c Änderungen

- **Ändern:** `#include "esp-now/network.h"` → `#include "esp-now.h"`

### 6. network/esp-now.h Änderungen

- Keine Änderungen am Inhalt

### 7. main/CMakeLists.txt Änderungen

```cmake
# Vorher:
INCLUDE_DIRS "." "lora" "esp-now"

# Nachher:
INCLUDE_DIRS "." "network"
```

## Wichtige Hinweise

- `lora_receiver_init()` wird komplett entfernt - nur `lora_start()` initialisiert den Receiver
- `init_lora()` wird in `lora.c` als statische Funktion oder öffentliche API verschoben
- Display-Update bleibt unverändert (bereits in `lora_rx_callback()`)
- ESP-NOW Display-Update bleibt unverändert

## TODO Liste

- [ ] Erstelle `main/network/` Verzeichnis
- [ ] Erstelle `main/network/esp-now.c` (von `esp-now/network.c`)
- [ ] Erstelle `main/network/esp-now.h` (von `esp-now/network.h`)
- [ ] Erstelle `main/network/lora.c` (von `lora/lora_receiver.c` + init_lora)
- [ ] Erstelle `main/network/lora.h` (von `lora/lora_receiver.h`)
- [ ] Aktualisiere `main/main.c` (Includes, entferne init_lora, entferne lora_receiver_init)
- [ ] Aktualisiere `main/CMakeLists.txt`
- [ ] Entferne alte Verzeichnisse `main/esp-now/` und `main/lora/`
- [ ] Build und verify
