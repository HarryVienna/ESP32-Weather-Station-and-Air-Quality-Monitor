#ifndef SENSOR_DECODER_H
#define SENSOR_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "sensor_stack.h"

/* ============================================================================
 * Sensor Decoder Framework
 * 
 * This module provides a transparent decoding interface for the ESP32-P4.
 * The P4 queries the decoder for field information and decodes payloads
 * based on the sensor_payload_format_t type.
 * 
 * Architecture:
 *   1. P4 reads packet from stack (sensor_stack_pop)
 *   2. P4 queries decoder for format info: sensor_decoder_get_format()
 *   3. P4 decodes payload: sensor_decoder_decode()
 *   4. P4 processes decoded values
 * ============================================================================ */

/* ============================================================================
 * Decoded sensor values (generic float array)
 * ============================================================================ */

#define MAX_DECODER_FIELDS  8

typedef struct {
    float values[MAX_DECODER_FIELDS];
    uint8_t field_count;
    const char* field_names[MAX_DECODER_FIELDS];
} sensor_values_t;

/* ============================================================================
 * Sensor format description (returned by decoder)
 * ============================================================================ */

typedef struct {
    sensor_payload_format_t format;
    const char* name;
    uint8_t expected_size;
    const char* description;
    const char* field_names[MAX_DECODER_FIELDS];
    uint8_t field_count;
} sensor_format_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize the sensor decoder module
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_init(void);

/**
 * @brief Register a decoder for a specific payload format
 * @param format The payload format to register for
 * @param fmt Pointer to format description (must persist)
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_register(sensor_payload_format_t format, const sensor_format_t* fmt);

/**
 * @brief Get the format description for a payload type
 * @param format The payload format to query
 * @return Pointer to format description, or NULL if not registered
 */
const sensor_format_t* sensor_decoder_get_format(sensor_payload_format_t format);

/**
 * @brief Decode a sensor payload into generic float values
 * @param format The payload format type
 * @param payload Pointer to raw payload bytes
 * @param payload_len Length of payload in bytes
 * @param values Output: decoded sensor values
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if format unknown
 */
esp_err_t sensor_decoder_decode(sensor_payload_format_t format, 
                                 const uint8_t* payload, 
                                 uint8_t payload_len,
                                 sensor_values_t* values);

/* ============================================================================
 * Built-in decoders (automatically registered by sensor_decoder_init)
 * ============================================================================ */

/**
 * @brief Decode BME280 payload: temp(4B) + hum(4B) + press(4B)
 * @param payload Pointer to 12 bytes of raw data
 * @param values Output: [0]=temperature, [1]=humidity, [2]=pressure
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_decode_bme280(const uint8_t* payload, sensor_values_t* values);

/**
 * @brief Decode HDC1080/DHT22 payload: temp(4B) + hum(4B)
 * @param payload Pointer to 8 bytes of raw data
 * @param values Output: [0]=temperature, [1]=humidity
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_decode_hdc1080(const uint8_t* payload, sensor_values_t* values);

/**
 * @brief Decode wind sensor payload: speed(4B) + direction(4B)
 * @param payload Pointer to 8 bytes of raw data
 * @param values Output: [0]=speed, [1]=direction
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_decode_wind(const uint8_t* payload, sensor_values_t* values);

/**
 * @brief Decode rain sensor payload: amount(4B)
 * @param payload Pointer to 4 bytes of raw data
 * @param values Output: [0]=rainfall_amount
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_decode_rain(const uint8_t* payload, sensor_values_t* values);

/**
 * @brief Decode light sensor payload: lux(4B)
 * @param payload Pointer to 4 bytes of raw data
 * @param values Output: [0]=illuminance
 * @return ESP_OK on success
 */
esp_err_t sensor_decoder_decode_light(const uint8_t* payload, sensor_values_t* values);

#endif /* SENSOR_DECODER_H */
