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
    ESP_LOGI(TAG, "Sensor stack initialized (size=%d, max_payload=%d)", 
             SENSOR_STACK_SIZE, MAX_PAYLOAD_SIZE);
    return ESP_OK;
}

esp_err_t sensor_stack_push(const sensor_packet_t *packet, sensor_source_t source) {
    if (packet == NULL || xSemaphoreTake(g_stack.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    // Validate payload length
    if (packet->header.payload_len > MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "Payload too large: %d bytes (max %d)", 
                 packet->header.payload_len, MAX_PAYLOAD_SIZE);
        xSemaphoreGive(g_stack.mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    if (g_stack.count >= SENSOR_STACK_SIZE) {
        g_stack.total_dropped++;
        xSemaphoreGive(g_stack.mutex);
        ESP_LOGW(TAG, "Stack full! Dropped packet #%lu (sensor %d, type %d)", 
                 g_stack.total_received + 1,
                 packet->header.sensor_nr,
                 packet->header.sensor_type);
        return ESP_ERR_NO_MEM;
    }

    uint8_t idx = (g_stack.head + 1) % SENSOR_STACK_SIZE;
    
    // Copy header (4 bytes)
    g_stack.packets[idx].header = packet->header;
    // Override source type
    g_stack.packets[idx].header.msg_type = (uint8_t)source;
    
    // Copy link metadata (12 bytes)
    g_stack.packets[idx].link = packet->link;
    
    // Copy variable payload (0..MAX_PAYLOAD_SIZE bytes)
    memcpy(g_stack.packets[idx].payload, packet->payload, packet->header.payload_len);

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
    
    // Copy header
    packet->header = g_stack.packets[idx].header;
    // Copy link metadata
    packet->link = g_stack.packets[idx].link;
    // Copy variable payload
    memcpy(packet->payload, g_stack.packets[idx].payload, 
           g_stack.packets[idx].header.payload_len);

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
