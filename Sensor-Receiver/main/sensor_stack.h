#ifndef SENSOR_STACK_H
#define SENSOR_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
 * Sensor Stack (FIFO buffer for I2C readout by ESP32-P4)
 * ============================================================================ */

#define SENSOR_STACK_SIZE     16
#define I2C_SLAVE_ADDR        0x38
#define I2C_CLOCK_KHZ         400

/* I2C Register Map for P4 readout */
#define I2C_REG_SENSOR_COUNT  0x00    // Number of available packets (1 byte)
#define I2C_REG_SENSOR_READ   0x01    // Read next packet (variable, up to 80 bytes)
#define I2C_REG_RESET_DROP    0x23    // Write 0x01 to reset dropped counter

typedef struct {
    sensor_packet_t packets[SENSOR_STACK_SIZE];
    uint8_t head;                   // Write index
    uint8_t tail;                   // Read index (for P4)
    uint8_t count;                  // Current number of packets
    uint32_t total_received;        // Total packets received
    uint32_t total_dropped;         // Dropped packets (stack full)
    SemaphoreHandle_t mutex;        // Access synchronization
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
 * @brief Push a received sensor packet into the stack
 * @param packet Pointer to the sensor packet (must have valid header + payload)
 * @param source SENSOR_SOURCE_LORA or SENSOR_SOURCE_ESPNOW
 * @return ESP_OK if stored, ESP_ERR_NO_MEM if stack full
 */
esp_err_t sensor_stack_push(const sensor_packet_t *packet, sensor_source_t source);

/**
 * @brief Read the next packet from the stack (consumes it)
 * @param packet Output buffer for the packet
 * @return ESP_OK if packet read, ESP_ERR_TIMEOUT if stack empty
 */
esp_err_t sensor_stack_pop(sensor_packet_t *packet);

/**
 * @brief Get the number of available packets
 * @return Number of packets in stack
 */
uint8_t sensor_stack_count(void);

/**
 * @brief Get statistics
 * @param received Output: total received
 * @param dropped Output: total dropped
 */
void sensor_stack_stats(uint32_t *received, uint32_t *dropped);

/**
 * @brief Reset the dropped counter
 */
void sensor_stack_reset_dropped(void);

#endif /* SENSOR_STACK_H */
