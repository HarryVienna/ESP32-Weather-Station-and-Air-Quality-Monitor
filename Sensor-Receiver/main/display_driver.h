#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "u8g2.h"
#include "sensor_stack.h"

/* ============================================================================
 * Display Configuration
 * ============================================================================ */

#define DISPLAY_FONT            u8g2_font_6x10_tr
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64

#define DISPLAY_HEADER_LINES    1   // Header-Zeile (Titel)
#define DISPLAY_ENTRY_LINES     4   // Sichtbare Einträge
#define DISPLAY_STATUS_LINES    1   // Status-Zeile (unten)

#define DISPLAY_LINE_HEIGHT     10
#define DISPLAY_HEADER_Y        10
#define DISPLAY_ENTRY_START_Y   20
#define DISPLAY_STATUS_Y        58

/* ============================================================================
 * Display Entry Structure
 * ============================================================================ */

typedef struct {
    uint8_t source;             // SENSOR_SOURCE_LORA or SENSOR_SOURCE_ESPNOW
    uint8_t sensor_nr;
    uint8_t sensor_type;
    int16_t rssi;               // -1 if ESP-NOW
    float snr;                  // -1.0 if ESP-NOW
    uint32_t timestamp;
} display_entry_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize the display driver (create mutex, set u8g2 pointer)
 * @param u8g2 Pointer to initialized U8g2 structure
 * @return ESP_OK on success
 */
esp_err_t display_driver_init(u8g2_t *u8g2);

/**
 * @brief Update display with new sensor data (called from callbacks)
 *        Scrolls upward when new entries arrive
 * @param packet Pointer to sensor packet (NULL = keep current display)
 * @return ESP_OK on success
 */
esp_err_t display_driver_update(const sensor_packet_t *packet);

/**
 * @brief Clear the display and show welcome message
 * @param u8g2 Pointer to initialized U8g2 structure
 */
void display_driver_show_welcome(u8g2_t *u8g2);

/**
 * @brief Show error message on display
 * @param u8g2 Pointer to initialized U8g2 structure
 * @param error_msg Error message string
 */
void display_driver_show_error(u8g2_t *u8g2, const char *error_msg);

/**
 * @brief Show status line (packet counts)
 * @param u8g2 Pointer to initialized U8g2 structure
 * @param lora_count LoRa packets received
 * @param espnow_count ESP-NOW packets received
 * @param dropped Dropped packets
 */
void display_driver_show_status(u8g2_t *u8g2, uint32_t lora_count, uint32_t espnow_count, uint32_t dropped);

#endif /* DISPLAY_DRIVER_H */
