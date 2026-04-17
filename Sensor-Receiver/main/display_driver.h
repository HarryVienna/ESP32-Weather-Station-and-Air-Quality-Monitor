#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "u8g2.h"
#include "sensor_stack.h"

/* ============================================================================
 * Display Configuration
 * ============================================================================ */

#define DISPLAY_FONT            u8g2_font_5x7_tr
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64

#define DISPLAY_BUFFER_SIZE     8   // Scroll-buffer: letzte Einträge
#define DISPLAY_ENTRY_LINES     8   // Sichtbare Einträge (max)

#define DISPLAY_LINE_HEIGHT     7
#define DISPLAY_ENTRY_START_Y   9

/* ============================================================================
 * Display Entry Structure (metadata only, no payload)
 * ============================================================================ */

typedef struct {
    uint8_t source;             // SENSOR_SOURCE_LORA or SENSOR_SOURCE_ESPNOW
    uint8_t sensor_nr;
    uint8_t sensor_type;
    uint8_t payload_len;        // Show payload size on display
    int16_t rssi;               // -1 if ESP-NOW
    float snr;                  // -1.0 if ESP-NOW
    uint32_t timestamp;
    char time_str[9];           // HH:MM:SS
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
 *        FIFO scroll buffer, newest at bottom
 * @param packet Pointer to sensor packet
 * @return ESP_OK on success
 */
esp_err_t display_driver_update(const sensor_packet_t *packet);

/**
 * @brief Show error message on display
 * @param u8g2 Pointer to initialized U8g2 structure
 * @param error_msg Error message string
 */
void display_driver_show_error(u8g2_t *u8g2, const char *error_msg);

#endif /* DISPLAY_DRIVER_H */
