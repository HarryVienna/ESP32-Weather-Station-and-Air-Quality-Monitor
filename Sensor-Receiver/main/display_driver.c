#include "display_driver.h"
#include "esp_log.h"
#include "inttypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char* TAG = "display";

static u8g2_t *g_u8g2;
static SemaphoreHandle_t g_display_mutex;

// FIFO scroll buffer: last DISPLAY_BUFFER_SIZE entries
static display_entry_t g_display_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t g_buffer_count;  // Number of valid entries (0..DISPLAY_BUFFER_SIZE)
static uint8_t g_write_idx;     // Next write position
static uint32_t g_lora_count;
static uint32_t g_espnow_count;
static uint32_t g_dropped;

/**
 * @brief Get current time as HH:MM:SS string from ESP32 system time
 */
static void get_current_time_str(char *buf, size_t buf_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%H:%M:%S", &timeinfo);
}

static const char* source_to_char(uint8_t source) {
    switch (source) {
        case SENSOR_SOURCE_LORA:    return "L";
        case SENSOR_SOURCE_ESPNOW:  return "N";
        default:                    return "?";
    }
}

static void draw_entry(int y, const display_entry_t *entry) {
    char line[42];
    
    // Format: "12:34:56 [L] 1 -87dBm +3.2dB" or "12:34:56 [N] 2 -65dBm"
    if (entry->source == SENSOR_SOURCE_LORA && entry->snr >= -40.0f) {
        snprintf(line, sizeof(line), "%s [%s] %d %ddBm %+.1fdB",
                         entry->time_str,
                         source_to_char(entry->source),
                         entry->sensor_nr,
                         entry->rssi,
                         (double)entry->snr);
    } else {
        snprintf(line, sizeof(line), "%s [%s] %d %ddBm",
                         entry->time_str,
                         source_to_char(entry->source),
                         entry->sensor_nr,
                         entry->rssi);
    }
    
    u8g2_DrawStr(g_u8g2, 1, y, line);
}

/**
 * @brief Refresh the display (draw empty list)
 *        Used for initial hardware initialization
 */
static void display_driver_refresh(void) {
    u8g2_ClearBuffer(g_u8g2);
    u8g2_SetFont(g_u8g2, DISPLAY_FONT);
    
    // No entries drawn - empty buffer
    u8g2_SendBuffer(g_u8g2);
}

esp_err_t display_driver_init(u8g2_t *u8g2) {
    g_u8g2 = u8g2;
    g_display_mutex = xSemaphoreCreateMutex();
    if (g_display_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return ESP_ERR_NO_MEM;
    }
    g_buffer_count = 0;
    g_write_idx = 0;
    g_lora_count = 0;
    g_espnow_count = 0;
    g_dropped = 0;
    memset(g_display_buffer, 0, sizeof(g_display_buffer));
    
    // Initial draw to prepare the display hardware - show empty list
    display_driver_refresh();
    
    ESP_LOGI(TAG, "Display driver initialized");
    return ESP_OK;
}

esp_err_t display_driver_update(const sensor_packet_t *packet) {
    if (g_u8g2 == NULL || g_display_mutex == NULL || packet == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Store entry in FIFO buffer
    display_entry_t *entry = &g_display_buffer[g_write_idx];
    
    entry->source = (packet->header.msg_type == SENSOR_SOURCE_LORA) ?
                    SENSOR_SOURCE_LORA : SENSOR_SOURCE_ESPNOW;
    entry->sensor_nr = packet->header.sensor_nr;
    entry->sensor_type = packet->header.sensor_type;
    entry->payload_len = packet->header.payload_len;
    entry->rssi = packet->link.lora_rssi;
    entry->snr = packet->link.lora_snr;
    entry->timestamp = packet->link.timestamp;
    
    // Use current time (ESP32 system time with timezone)
    get_current_time_str(entry->time_str, sizeof(entry->time_str));
    
    // Update counters
    if (entry->source == SENSOR_SOURCE_LORA) {
        g_lora_count++;
    } else {
        g_espnow_count++;
    }
    
    // Advance write index
    g_write_idx = (g_write_idx + 1) % DISPLAY_BUFFER_SIZE;
    
    // Update count (max DISPLAY_BUFFER_SIZE)
    if (g_buffer_count < DISPLAY_BUFFER_SIZE) {
        g_buffer_count++;
    }
    
    // Draw display
    u8g2_ClearBuffer(g_u8g2);
    u8g2_SetFont(g_u8g2, DISPLAY_FONT);
    
    // Draw entries: oldest first, newest at bottom
    int visible_count = g_buffer_count < DISPLAY_ENTRY_LINES ?
                         g_buffer_count : DISPLAY_ENTRY_LINES;
    
    // Calculate start index for visible entries
    int start_idx;
    if (g_buffer_count <= DISPLAY_BUFFER_SIZE) {
        // Not full yet: show from oldest
        start_idx = (g_write_idx - g_buffer_count + DISPLAY_BUFFER_SIZE) % DISPLAY_BUFFER_SIZE;
    } else {
        // Full buffer: show last DISPLAY_ENTRY_LINES entries
        start_idx = (g_write_idx - visible_count + DISPLAY_BUFFER_SIZE) % DISPLAY_BUFFER_SIZE;
    }
    
    for (int i = 0; i < visible_count; i++) {
        int idx = (start_idx + i) % DISPLAY_BUFFER_SIZE;
        int y = DISPLAY_ENTRY_START_Y + (i * DISPLAY_LINE_HEIGHT);
        draw_entry(y, &g_display_buffer[idx]);
    }
    
    u8g2_SendBuffer(g_u8g2);
    
    xSemaphoreGive(g_display_mutex);
    return ESP_OK;
}

