#include "sensor_stack.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "sensor_stack";

static sensor_stack_t g_stack;
static SemaphoreHandle_t g_mutex;

esp_err_t sensor_stack_init(void) {
    memset(&g_stack, 0, sizeof(g_stack));
    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Sensor stack initialized (max_sensors=%d, max_payload=%d)",
             MAX_SENSORS, MAX_PAYLOAD_SIZE);
    return ESP_OK;
}

esp_err_t sensor_stack_push(const sensor_packet_t *packet, sensor_source_t source) {
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t sensor_nr = packet->header.sensor_nr;

    if (sensor_nr >= MAX_SENSORS) {
        ESP_LOGE(TAG, "sensor_nr %d out of range, ignored (max %d)", sensor_nr, MAX_SENSORS - 1);
        return ESP_ERR_INVALID_ARG;
    }

    if (packet->header.payload_len > MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "Payload too large: %d bytes (max %d)",
                 packet->header.payload_len, MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    sensor_slot_t *slot = &g_stack.slots[sensor_nr];

    if (!slot->valid) {
        g_stack.count++;
    } else {
        g_stack.total_overwritten++;
        ESP_LOGD(TAG, "Sensor %d: unread data overwritten", sensor_nr);
    }

    slot->packet = *packet;
    slot->valid  = true;
    g_stack.total_received++;

    xSemaphoreGive(g_mutex);
    return ESP_OK;
}

esp_err_t sensor_stack_pop(sensor_packet_t *packet) {
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (g_stack.slots[i].valid) {
            *packet = g_stack.slots[i].packet;
            g_stack.slots[i].valid = false;
            g_stack.count--;
            xSemaphoreGive(g_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(g_mutex);
    return ESP_ERR_NOT_FOUND;
}

uint8_t sensor_stack_count(void) {
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    uint8_t count = g_stack.count;
    xSemaphoreGive(g_mutex);
    return count;
}

void sensor_stack_stats(uint32_t *received, uint32_t *overwritten) {
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (received)    *received    = 0;
        if (overwritten) *overwritten = 0;
        return;
    }
    if (received)    *received    = g_stack.total_received;
    if (overwritten) *overwritten = g_stack.total_overwritten;
    xSemaphoreGive(g_mutex);
}

void sensor_stack_reset_dropped(void) {
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    g_stack.total_overwritten = 0;
    xSemaphoreGive(g_mutex);
}
