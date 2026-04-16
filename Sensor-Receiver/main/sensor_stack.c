#include "sensor_stack.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "sensor_stack";

static sensor_stack_t g_stack;

esp_err_t sensor_stack_init(void) {
    memset(&g_stack, 0, sizeof(g_stack));
    g_stack.mutex = xSemaphoreCreateMutex();
    if (g_stack.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Sensor stack initialized (size=%d)", SENSOR_STACK_SIZE);
    return ESP_OK;
}

esp_err_t sensor_stack_push(const sensor_packet_t *packet, sensor_source_t source) {
    if (packet == NULL || xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    if (g_stack.count >= SENSOR_STACK_SIZE) {
        g_stack.total_dropped++;
        xSemaphoreGive(g_stack.mutex);
        ESP_LOGW(TAG, "Stack full! Dropped packet #%lu", g_stack.total_received + 1);
        return ESP_ERR_NO_MEM;
    }

    uint8_t idx = (g_stack.head + 1) % SENSOR_STACK_SIZE;
    g_stack.packets[idx].msg_type = (uint8_t)source;
    g_stack.packets[idx].sensor_nr = packet->sensor_nr;
    g_stack.packets[idx].sensor_type = packet->sensor_type;
    g_stack.packets[idx].voltage_mv = packet->voltage_mv;
    g_stack.packets[idx].temperature = packet->temperature;
    g_stack.packets[idx].humidity = packet->humidity;
    g_stack.packets[idx].pressure = packet->pressure;
    g_stack.packets[idx].lora_rssi = packet->lora_rssi;
    g_stack.packets[idx].lora_snr = packet->lora_snr;
    g_stack.packets[idx].timestamp = packet->timestamp;

    g_stack.head = idx;
    g_stack.count++;
    g_stack.total_received++;

    xSemaphoreGive(g_stack.mutex);
    return ESP_OK;
}

esp_err_t sensor_stack_pop(sensor_packet_t *packet) {
    if (packet == NULL || xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    if (g_stack.count == 0) {
        xSemaphoreGive(g_stack.mutex);
        return ESP_ERR_TIMEOUT;
    }

    uint8_t idx = (g_stack.tail + 1) % SENSOR_STACK_SIZE;
    packet->msg_type = g_stack.packets[idx].msg_type;
    packet->sensor_nr = g_stack.packets[idx].sensor_nr;
    packet->sensor_type = g_stack.packets[idx].sensor_type;
    packet->voltage_mv = g_stack.packets[idx].voltage_mv;
    packet->temperature = g_stack.packets[idx].temperature;
    packet->humidity = g_stack.packets[idx].humidity;
    packet->pressure = g_stack.packets[idx].pressure;
    packet->lora_rssi = g_stack.packets[idx].lora_rssi;
    packet->lora_snr = g_stack.packets[idx].lora_snr;
    packet->timestamp = g_stack.packets[idx].timestamp;

    g_stack.tail = idx;
    g_stack.count--;

    xSemaphoreGive(g_stack.mutex);
    return ESP_OK;
}

uint8_t sensor_stack_count(void) {
    if (xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    uint8_t count = g_stack.count;
    xSemaphoreGive(g_stack.mutex);
    return count;
}

void sensor_stack_stats(uint32_t *received, uint32_t *dropped) {
    if (xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (received) *received = 0;
        if (dropped) *dropped = 0;
        return;
    }
    if (received) *received = g_stack.total_received;
    if (dropped) *dropped = g_stack.total_dropped;
    xSemaphoreGive(g_stack.mutex);
}

void sensor_stack_reset_dropped(void) {
    if (xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    g_stack.total_dropped = 0;
    xSemaphoreGive(g_stack.mutex);
}
