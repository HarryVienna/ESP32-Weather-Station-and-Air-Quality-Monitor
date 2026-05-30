/**
 * @file packet_format.h
 * @brief Gemeinsame Packet-Struktur für alle Sender und den Receiver
 *
 * Drei-Schichten Packet Design:
 * 1. packet_header_t  ( 4 Byte) - Immer vom Sender geschickt
 * 2. link_metadata_t  (11 Byte) - Nur receiver-intern, nie über Funk gesendet
 * 3. payload[]        (0-64 Byte) - Raw-Daten
 *
 * Sender schicken: packet_header_t + payload[]
 * Receiver baut:  sensor_packet_t (header + link_metadata + payload) intern auf
 */

#ifndef PACKET_FORMAT_H
#define PACKET_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Sensor Source (how data reached the receiver)
 * ============================================================================ */

typedef enum {
    SENSOR_SOURCE_LORA   = 1,
    SENSOR_SOURCE_ESPNOW = 2
} sensor_source_t;

/* ============================================================================
 * Sensor Types (identifies the sensor hardware)
 * ============================================================================ */

typedef enum {
    SENSOR_TYPE_BME280  = 1,   // Temperature, Humidity, Pressure
    SENSOR_TYPE_HDC1080 = 2,   // Temperature, Humidity
    SENSOR_TYPE_DHT22   = 3,   // Temperature, Humidity
    SENSOR_TYPE_WIND    = 4,   // Wind speed, direction
    SENSOR_TYPE_RAIN    = 5,   // Rain collector
    SENSOR_TYPE_LIGHT   = 6,   // Light sensor
    SENSOR_TYPE_CUSTOM  = 255  // Proprietary format
} sensor_type_t;

/* ============================================================================
 * Layer 1: Packet Header (4 bytes) — sent by every sender
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t msg_type;     // DATA=2, PAIRING_REQ=0, etc.
    uint8_t sensor_nr;    // 1-254 (0 and 255 reserved)
    uint8_t sensor_type;  // sensor_type_t
    uint8_t payload_len;  // Length of payload[] in bytes (0-64)
} packet_header_t;

/* ============================================================================
 * Layer 2: Link Metadata (11 bytes) — receiver-internal only, never sent over air
 *   uint8_t  msg_source  = 1 byte
 *   int16_t  lora_rssi   = 2 bytes
 *   float    lora_snr    = 4 bytes
 *   uint32_t timestamp   = 4 bytes
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  msg_source;  // sensor_source_t: LORA=1, ESPNOW=2
    int16_t  rssi;   // RSSI in dBm
    float    snr;    // SNR  in dB   (-1.0   if ESP-NOW)
    uint32_t timestamp;   // xTaskGetTickCount() at receive time
} link_metadata_t;

/* ============================================================================
 * Layer 3: Full internal packet (receiver only)
 * ============================================================================ */

#define MAX_PAYLOAD_SIZE 64

typedef struct __attribute__((packed)) {
    packet_header_t header;              //  4 bytes
    link_metadata_t link;                // 11 bytes
    uint8_t         payload[MAX_PAYLOAD_SIZE]; // 0-64 bytes
} sensor_packet_t;

/* ============================================================================
 * Size constants
 * ============================================================================ */

#define PACKET_HEADER_SIZE   sizeof(packet_header_t)   //  4 bytes
#define PACKET_METADATA_SIZE sizeof(link_metadata_t)   // 11 bytes
#define PACKET_MIN_SIZE      (PACKET_HEADER_SIZE + PACKET_METADATA_SIZE)               // 15 bytes
#define PACKET_MAX_SIZE      (PACKET_HEADER_SIZE + PACKET_METADATA_SIZE + MAX_PAYLOAD_SIZE) // 79 bytes

/* ============================================================================
 * Payload definitions per sensor type
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint32_t voltage;      // mV
    float    pressure;     // hPa
    float    temperature;  // °C
    float    humidity;     // %
} bme280_payload_t;

/* ============================================================================
 * Sender packet types (header + payload only, no link_metadata)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    packet_header_t header;
    uint8_t         payload[MAX_PAYLOAD_SIZE];
} espnow_sensor_packet_t;

typedef struct __attribute__((packed)) {
    packet_header_t header;
    uint8_t         payload[MAX_PAYLOAD_SIZE];
} lora_sensor_packet_t;

#endif /* PACKET_FORMAT_H */
