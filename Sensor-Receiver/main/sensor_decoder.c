#include "sensor_decoder.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "sensor_decoder";

/* ============================================================================
 * Internal decoder registry
 * ============================================================================ */

#define MAX_DECODERS 16
static sensor_format_t* g_decoders[MAX_DECODERS];
static uint8_t g_decoder_count;

/* ============================================================================
 * Format descriptions for built-in decoders
 * ============================================================================ */

static const sensor_format_t g_format_bme280 = {
    .format = SENSOR_PAYLOAD_BME280,
    .name = "BME280",
    .expected_size = 12,
    .description = "Temperature, Humidity, Pressure",
    .field_names = {"temperature", "humidity", "pressure"},
    .field_count = 3
};

static const sensor_format_t g_format_hdc1080 = {
    .format = SENSOR_PAYLOAD_HDC1080,
    .name = "HDC1080",
    .expected_size = 8,
    .description = "Temperature, Humidity",
    .field_names = {"temperature", "humidity"},
    .field_count = 2
};

static const sensor_format_t g_format_dht22 = {
    .format = SENSOR_PAYLOAD_DHT22,
    .name = "DHT22",
    .expected_size = 8,
    .description = "Temperature, Humidity",
    .field_names = {"temperature", "humidity"},
    .field_count = 2
};

static const sensor_format_t g_format_wind = {
    .format = SENSOR_PAYLOAD_WIND,
    .name = "Wind",
    .expected_size = 8,
    .description = "Wind speed, Direction",
    .field_names = {"speed", "direction"},
    .field_count = 2
};

static const sensor_format_t g_format_rain = {
    .format = SENSOR_PAYLOAD_RAIN,
    .name = "Rain",
    .expected_size = 4,
    .description = "Rainfall amount",
    .field_names = {"amount"},
    .field_count = 1
};

static const sensor_format_t g_format_light = {
    .format = SENSOR_PAYLOAD_LIGHT,
    .name = "Light",
    .expected_size = 4,
    .description = "Illuminance",
    .field_names = {"lux"},
    .field_count = 1
};

/* ============================================================================
 * Built-in decoder implementations
 * ============================================================================ */

esp_err_t sensor_decoder_decode_bme280(const uint8_t* payload, sensor_values_t* values) {
    if (payload == NULL || values == NULL) return ESP_ERR_INVALID_ARG;
    if (payload[0] != SENSOR_PAYLOAD_BME280) return ESP_ERR_NOT_SUPPORTED;
    
    // Expect 12 bytes: temp(4) + hum(4) + press(4)
    if (payload[1] != 12) return ESP_ERR_INVALID_SIZE;
    
    uint8_t offset = 2;  // Skip format byte + length byte
    
    memcpy(&values->values[0], payload + offset, 4);
    offset += 4;
    values->field_names[0] = "temperature";
    
    memcpy(&values->values[1], payload + offset, 4);
    offset += 4;
    values->field_names[1] = "humidity";
    
    memcpy(&values->values[2], payload + offset, 4);
    offset += 4;
    values->field_names[2] = "pressure";
    
    values->field_count = 3;
    return ESP_OK;
}

esp_err_t sensor_decoder_decode_hdc1080(const uint8_t* payload, sensor_values_t* values) {
    if (payload == NULL || values == NULL) return ESP_ERR_INVALID_ARG;
    if (payload[0] != SENSOR_PAYLOAD_HDC1080) return ESP_ERR_NOT_SUPPORTED;
    
    // Expect 8 bytes: temp(4) + hum(4)
    if (payload[1] != 8) return ESP_ERR_INVALID_SIZE;
    
    uint8_t offset = 2;
    
    memcpy(&values->values[0], payload + offset, 4);
    offset += 4;
    values->field_names[0] = "temperature";
    
    memcpy(&values->values[1], payload + offset, 4);
    offset += 4;
    values->field_names[1] = "humidity";
    
    values->field_count = 2;
    return ESP_OK;
}

esp_err_t sensor_decoder_decode_wind(const uint8_t* payload, sensor_values_t* values) {
    if (payload == NULL || values == NULL) return ESP_ERR_INVALID_ARG;
    if (payload[0] != SENSOR_PAYLOAD_WIND) return ESP_ERR_NOT_SUPPORTED;
    
    // Expect 8 bytes: speed(4) + dir(4)
    if (payload[1] != 8) return ESP_ERR_INVALID_SIZE;
    
    uint8_t offset = 2;
    
    memcpy(&values->values[0], payload + offset, 4);
    offset += 4;
    values->field_names[0] = "speed";
    
    memcpy(&values->values[1], payload + offset, 4);
    offset += 4;
    values->field_names[1] = "direction";
    
    values->field_count = 2;
    return ESP_OK;
}

esp_err_t sensor_decoder_decode_rain(const uint8_t* payload, sensor_values_t* values) {
    if (payload == NULL || values == NULL) return ESP_ERR_INVALID_ARG;
    if (payload[0] != SENSOR_PAYLOAD_RAIN) return ESP_ERR_NOT_SUPPORTED;
    
    // Expect 4 bytes: amount(4)
    if (payload[1] != 4) return ESP_ERR_INVALID_SIZE;
    
    uint8_t offset = 2;
    
    memcpy(&values->values[0], payload + offset, 4);
    values->field_names[0] = "amount";
    
    values->field_count = 1;
    return ESP_OK;
}

esp_err_t sensor_decoder_decode_light(const uint8_t* payload, sensor_values_t* values) {
    if (payload == NULL || values == NULL) return ESP_ERR_INVALID_ARG;
    if (payload[0] != SENSOR_PAYLOAD_LIGHT) return ESP_ERR_NOT_SUPPORTED;
    
    // Expect 4 bytes: lux(4)
    if (payload[1] != 4) return ESP_ERR_INVALID_SIZE;
    
    uint8_t offset = 2;
    
    memcpy(&values->values[0], payload + offset, 4);
    values->field_names[0] = "lux";
    
    values->field_count = 1;
    return ESP_OK;
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

esp_err_t sensor_decoder_init(void) {
    memset(g_decoders, 0, sizeof(g_decoders));
    g_decoder_count = 0;
    
    // Register built-in decoders
    sensor_decoder_register(SENSOR_PAYLOAD_BME280, &g_format_bme280);
    sensor_decoder_register(SENSOR_PAYLOAD_HDC1080, &g_format_hdc1080);
    sensor_decoder_register(SENSOR_PAYLOAD_DHT22, &g_format_dht22);
    sensor_decoder_register(SENSOR_PAYLOAD_WIND, &g_format_wind);
    sensor_decoder_register(SENSOR_PAYLOAD_RAIN, &g_format_rain);
    sensor_decoder_register(SENSOR_PAYLOAD_LIGHT, &g_format_light);
    
    ESP_LOGI(TAG, "Sensor decoder initialized (%d decoders registered)", g_decoder_count);
    return ESP_OK;
}

esp_err_t sensor_decoder_register(sensor_payload_format_t format, const sensor_format_t* fmt) {
    if (fmt == NULL || g_decoder_count >= MAX_DECODERS) {
        return ESP_ERR_NO_MEM;
    }
    
    // Check for duplicate
    for (uint8_t i = 0; i < g_decoder_count; i++) {
        if (g_decoders[i]->format == format) {
            ESP_LOGW(TAG, "Decoder for format %d already registered", format);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    g_decoders[g_decoder_count++] = (sensor_format_t*)fmt;
    return ESP_OK;
}

const sensor_format_t* sensor_decoder_get_format(sensor_payload_format_t format) {
    for (uint8_t i = 0; i < g_decoder_count; i++) {
        if (g_decoders[i]->format == format) {
            return g_decoders[i];
        }
    }
    return NULL;
}

esp_err_t sensor_decoder_decode(sensor_payload_format_t format, 
                                 const uint8_t* payload, 
                                 uint8_t payload_len,
                                 sensor_values_t* values) {
    if (values == NULL) return ESP_ERR_INVALID_ARG;
    memset(values, 0, sizeof(sensor_values_t));
    
    const sensor_format_t* fmt = sensor_decoder_get_format(format);
    if (fmt == NULL) {
        ESP_LOGW(TAG, "No decoder registered for format %d", format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (payload == NULL) {
        // No payload, return empty values
        return ESP_OK;
    }
    
    if (payload_len < fmt->expected_size) {
        ESP_LOGW(TAG, "Payload too small for format %s: expected %d, got %d",
                 fmt->name, fmt->expected_size, payload_len);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Dispatch to appropriate decoder
    switch (format) {
        case SENSOR_PAYLOAD_BME280:
            return sensor_decoder_decode_bme280(payload, values);
            
        case SENSOR_PAYLOAD_HDC1080:
        case SENSOR_PAYLOAD_DHT22:
            return sensor_decoder_decode_hdc1080(payload, values);
            
        case SENSOR_PAYLOAD_WIND:
            return sensor_decoder_decode_wind(payload, values);
            
        case SENSOR_PAYLOAD_RAIN:
            return sensor_decoder_decode_rain(payload, values);
            
        case SENSOR_PAYLOAD_LIGHT:
            return sensor_decoder_decode_light(payload, values);
            
        case SENSOR_PAYLOAD_CUSTOM:
            // Custom decoders must be registered by the user
            ESP_LOGW(TAG, "Custom payload format - no default decoder");
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            ESP_LOGW(TAG, "Unknown payload format %d", format);
            return ESP_ERR_NOT_SUPPORTED;
    }
}
