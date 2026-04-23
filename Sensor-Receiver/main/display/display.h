#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "u8g2.h"
#include "sensor_stack.h"

/* ============================================================================
 * Hardware Pins (Heltec WiFi LoRa 32 V3.x / V4)
 * ============================================================================ */

#define DISPLAY_PIN_SDA     GPIO_NUM_17
#define DISPLAY_PIN_SCL     GPIO_NUM_18
#define DISPLAY_PIN_RST     GPIO_NUM_21

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
    uint8_t payload_len;
    int16_t rssi;
    float snr;
    uint32_t timestamp;
    char time_str[6];           // HH:MM
} display_entry_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize display hardware and driver (owns u8g2 internally)
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * @brief Append a new line for the received sensor packet (scrolls when full)
 * @param packet Pointer to sensor packet
 * @return ESP_OK on success
 */
esp_err_t display_update(const sensor_packet_t *packet);

#endif /* DISPLAY_H */
