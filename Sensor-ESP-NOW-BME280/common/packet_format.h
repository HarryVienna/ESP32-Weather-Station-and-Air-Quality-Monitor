/**
 * @file packet_format.h
 * @brief Gemeinsame Packet-Struktur für Sender und Receiver
 * 
 * Diese Datei muss in BEIDE Projekte kopiert werden:
 * - Sensor-Receiver (ESP32-S3): main/packet_format.h
 * - Sensor-Sender (ESP32): entsprechend einfügen
 * 
 * Drei-Schichten Packet Design:
 * 1. packet_header_t (4 Byte)  - Immer bekannt vom Receiver
 * 2. link_metadata_t (16 Byte) - LoRa/ESP-NOW Metadaten + msg_source
 * 3. payload[] (0-64 Byte)     - Raw-Daten, Inhalt unbekannt
 */

#ifndef PACKET_FORMAT_H
#define PACKET_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Sensor Source (how data reached the receiver)
 * ============================================================================ */

typedef enum {
    SENSOR_SOURCE_LORA = 1,
    SENSOR_SOURCE_ESPNOW = 2
} sensor_source_t;

/* ============================================================================
 * Sensor Types (identifies the sensor hardware)
 * ============================================================================ */

typedef enum {
    SENSOR_TYPE_BME280 = 1,   // Temperature, Humidity, Pressure
    SENSOR_TYPE_HDC1080 = 2,  // Temperature, Humidity
    SENSOR_TYPE_DHT22 = 3,    // Temperature, Humidity
    SENSOR_TYPE_WIND = 4,     // Wind speed, direction
    SENSOR_TYPE_RAIN = 5,     // Rain collector
    SENSOR_TYPE_LIGHT = 6,    // Light sensor
    SENSOR_TYPE_CUSTOM = 255  // Proprietary format
} sensor_type_t;

/* ============================================================================
 * Layer 1: Packet Header (4 bytes) - ALWAYS known by receiver
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t msg_type;           // Original sender msg_type (DATA=2, PAIRING_REQ=0, etc.)
    uint8_t sensor_nr;          // 1-254 (0 and 255 reserved)
    uint8_t sensor_type;        // sensor_type_t
    uint8_t payload_len;        // Length of payload in bytes (0-64)
} packet_header_t;

/* ============================================================================
 * Layer 2: Link Metadata (16 bytes) - Transport-specific info
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t msg_source;         // sensor_source_t: how data reached receiver (LORA=1, ESPNOW=2)
    int16_t lora_rssi;         // RSSI in dBm (-32768 if ESP-NOW or invalid)
    float lora_snr;            // SNR in dB (-1.0 if ESP-NOW or invalid)
    uint32_t timestamp;        // xTaskGetTickCount() at receive time
} link_metadata_t;

/* ============================================================================
 * Layer 3: Full Packet (variable size)
 * ============================================================================ */

#define MAX_PAYLOAD_SIZE 64

typedef struct __attribute__((packed)) {
    packet_header_t header;         // 4 bytes - always known
    link_metadata_t link;           // 16 bytes - transport metadata
    uint8_t payload[MAX_PAYLOAD_SIZE];  // 0-64 bytes - raw data
} sensor_packet_t;

/* ============================================================================
 * Packet Size Constants (for compile-time validation)
 * ============================================================================ */

#define PACKET_HEADER_SIZE    sizeof(packet_header_t)     // 4 bytes
#define PACKET_METADATA_SIZE  sizeof(link_metadata_t)     // 16 bytes
#define PACKET_MIN_SIZE       (PACKET_HEADER_SIZE + PACKET_METADATA_SIZE)  // 20 bytes
#define PACKET_MAX_SIZE       (PACKET_HEADER_SIZE + PACKET_METADATA_SIZE + MAX_PAYLOAD_SIZE)  // 84 bytes

/* ============================================================================
 * ESP-NOW Sender Packet (header + variable payload, no metadata)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    packet_header_t header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} espnow_sensor_packet_t;

#endif /* PACKET_FORMAT_H */
