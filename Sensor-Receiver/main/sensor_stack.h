#ifndef SENSOR_STACK_H
#define SENSOR_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

/* ============================================================================
 * Common packet format - shared with sender project
 * Copy common/packet_format.h into your sender project as well
 * ============================================================================ */
#include "../common/packet_format.h"

/* ============================================================================
 * Payload format enum (P4-only, not needed by sender)
 * ============================================================================ */

typedef enum {
    SENSOR_PAYLOAD_NONE    = 0,   // No payload (e.g., beacon)
    SENSOR_PAYLOAD_BME280  = 1,   // temp(4) + hum(4) + press(4) = 12 bytes
    SENSOR_PAYLOAD_HDC1080 = 2,   // temp(4) + hum(4) = 8 bytes
    SENSOR_PAYLOAD_DHT22   = 3,   // temp(4) + hum(4) = 8 bytes
    SENSOR_PAYLOAD_WIND    = 4,   // speed(4) + dir(4) = 8 bytes
    SENSOR_PAYLOAD_RAIN    = 5,   // amount(4) = 4 bytes
    SENSOR_PAYLOAD_LIGHT   = 6,   // lux(4) = 4 bytes
    SENSOR_PAYLOAD_CUSTOM  = 255, // Variable format, defined by sensor_type
} sensor_payload_format_t;

/* ============================================================================
 * Sensor Stack: one slot per sensor, latest value wins
 * New data from the same sensor overwrites the previous unread value.
 * Reading a slot (pop) marks it as consumed.
 * ============================================================================ */

#define MAX_SENSORS           16   // Maximum number of distinct sensors
#define I2C_SLAVE_ADDR        0x38
#define I2C_CLOCK_KHZ         400

/* I2C Register Map for P4 readout */
#define I2C_REG_SENSOR_COUNT  0x00    // Number of sensors with unread data (1 byte)
#define I2C_REG_SENSOR_READ   0x01    // Read and consume next available packet
#define I2C_REG_RESET_DROP    0x23    // Write 0x01 to reset dropped counter

typedef struct {
    sensor_packet_t packet;
    bool valid;                     // true = unread data available
} sensor_slot_t;

typedef struct {
    sensor_slot_t slots[MAX_SENSORS];  // indexed directly by sensor_nr
    uint8_t count;                     // number of slots with unread data
    uint32_t total_received;
    uint32_t total_overwritten;        // replaced unread data (same sensor updated)
} sensor_stack_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize the sensor stack
 * @return ESP_OK on success
 */
esp_err_t sensor_stack_init(void);

/**
 * @brief Push a sensor packet into the stack.
 *        If a slot for this sensor_nr already exists, it is overwritten.
 *        If no slot exists yet, a free slot is allocated.
 * @param packet Pointer to the sensor packet (must have valid header + payload)
 * @param source SENSOR_SOURCE_LORA or SENSOR_SOURCE_ESPNOW
 * @return ESP_OK on success, ESP_ERR_NO_MEM if MAX_SENSORS limit reached
 */
esp_err_t sensor_stack_push(const sensor_packet_t *packet, sensor_source_t source);

/**
 * @brief Read and consume the next available packet.
 *        After this call the slot is marked free (for a new sensor if needed).
 * @param packet Output buffer for the packet
 * @return ESP_OK if a packet was read, ESP_ERR_NOT_FOUND if stack empty
 */
esp_err_t sensor_stack_pop(sensor_packet_t *packet);

/**
 * @brief Get the number of sensors with unread data
 * @return Number of valid (unread) slots
 */
uint8_t sensor_stack_count(void);

/**
 * @brief Get statistics
 * @param received    Output: total packets received
 * @param overwritten Output: packets that replaced unread data (same sensor updated)
 */
void sensor_stack_stats(uint32_t *received, uint32_t *overwritten);

/**
 * @brief Reset the dropped and overwritten counters
 */
void sensor_stack_reset_dropped(void);

#endif /* SENSOR_STACK_H */
