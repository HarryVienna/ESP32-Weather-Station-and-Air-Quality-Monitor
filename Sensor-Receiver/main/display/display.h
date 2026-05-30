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
  * Display Async Update via Queue
  * ============================================================================
   * Sensor-Callback (LoRa/ESP-NOW) ruft display_update_async() auf, das eine
   * Kopie der Packet-Daten in eine FreeRTOS-Queue schreibt. Ein separater
   * Display-Task liest von dieser Queue und aktualisiert das Display.
   *
   * Vorteil: Der Sensor-Callback muss NICHT auf den Display-Mutex warten
   * und kehnt sofort nach dem Queue-Eintritt zurueck.
  * ============================================================================ */

#define DISPLAY_QUEUE_MAX_ENTRIES  8  // Max pending display updates

typedef struct {
    uint8_t source;
    uint8_t sensor_nr;
    uint8_t sensor_type;
    uint8_t payload_len;
    int16_t rssi;
    float   snr;
    uint32_t timestamp;
    char    time_str[6];
} display_queue_entry_t;

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
 * This function copies packet metadata into a FreeRTOS queue. A separate
 * display task reads from the queue and updates the display. The caller
 * does NOT wait for the actual display update.
 */
esp_err_t display_update_async(const sensor_packet_t *packet);

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
