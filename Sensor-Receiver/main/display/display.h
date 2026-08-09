#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "u8g2.h"
#include "sensor_stack.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ============================================================================
 * Hardware Pins (Heltec WiFi LoRa 32 V3.x / V4)
 * ============================================================================ */

#define DISPLAY_PIN_SDA     GPIO_NUM_17
#define DISPLAY_PIN_SCL     GPIO_NUM_18
#define DISPLAY_PIN_RST     GPIO_NUM_21
#define DISPLAY_BUTTON_PIN  GPIO_NUM_0
#define DISPLAY_LED_PIN     GPIO_NUM_35

/* ============================================================================
 * Display Configuration
 * ============================================================================ */

#define DISPLAY_FONT            u8g2_font_5x7_tr
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64

#define DISPLAY_BUFFER_SIZE     8   // Scroll buffer: most recent entries
#define DISPLAY_ENTRY_LINES     8   // Visible entries (max)

#define DISPLAY_LINE_HEIGHT     7
#define DISPLAY_ENTRY_START_Y   9

/* ============================================================================
 * Display Line (already-formatted text, no raw sensor/event fields)
 * ============================================================================
 * The scroll buffer and the update queue only ever hold ready-to-draw text.
 * Producers (sensor callbacks via display_update_async(), or any other
 * caller via display_log_line()) do their own formatting up front - the
 * display task and redraw() just show a list of strings and don't need to
 * know anything about sensors, OTA, or any other source of a line.
 * ============================================================================ */

#define DISPLAY_LINE_MAX_LEN  42

typedef struct {
    char text[DISPLAY_LINE_MAX_LEN];
} display_line_t;

#define DISPLAY_QUEUE_MAX_ENTRIES  8  // Max pending display updates

/* ============================================================================
  * API Functions
  * ============================================================================ */

/**
 * @brief Append a new line for the received sensor packet (scrolls when full)
 * @param packet Pointer to sensor packet
 * @return ESP_OK on success
 */
esp_err_t display_update(const sensor_packet_t *packet);

/**
 * @brief Async display update - non-blocking, writes to internal queue
 * @param packet Pointer to sensor packet
 * @return ESP_OK if enqueued successfully, ESP_ERR_TIMEOUT if queue full
 *
 * This function formats the packet into a text line and writes it into a
 * FreeRTOS queue. A separate display task reads from the queue and updates
 * the display. The caller does NOT wait for the actual display update.
 */
esp_err_t display_update_async(const sensor_packet_t *packet);

/**
 * @brief Append an arbitrary, already-formatted text line (e.g. OTA status)
 * @param text NUL-terminated line, truncated to DISPLAY_LINE_MAX_LEN - 1
 * @return ESP_OK if enqueued successfully, ESP_ERR_TIMEOUT if queue full
 *
 * Same non-blocking queue path as display_update_async() - just for callers
 * that already have their own text instead of a sensor_packet_t (e.g.
 * ota/ota_task.c logging "Update to x.y.z").
 */
esp_err_t display_log_line(const char *text);

/**
 * @brief Toggle display on/off
 *
 * If the display is currently on, it will be turned off (power save mode).
 * If the display is currently off, it will be turned on and redrawn.
 */
void display_toggle(void);

/**
 * @brief Initialize display with integrated button for toggle
 *
 * This function initializes the display hardware and creates the button
 * handler automatically. Use this instead of display_init() if you want
 * the button to control the display directly from within the display module.
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

#endif /* DISPLAY_H */
