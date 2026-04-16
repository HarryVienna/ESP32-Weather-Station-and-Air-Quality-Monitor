#include "display_driver.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "display";

static u8g2_t *g_u8g2;
static SemaphoreHandle_t g_display_mutex;

// Scroll buffer: keep last N entries for smooth scrolling
#define DISPLAY_BUFFER_SIZE 8
static display_entry_t g_display_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t g_buffer_count;
static uint32_t g_lora_count;
static uint32_t g_espnow_count;
static uint32_t g_dropped;

static const char* source_to_string(uint8_t source) {
    switch (source) {
        case SENSOR_SOURCE_LORA:    return "LoRa";
        case SENSOR_SOURCE_ESPNOW:  return "ESPN";
        default:                    return "???";
    }
}

static const char* sensor_type_to_string(uint8_t type) {
    switch (type) {
        case SENSOR_TYPE_BME280:  return "BME280";
        case SENSOR_TYPE_HDC1080: return "HDC1080";
        case SENSOR_TYPE_DHT22:   return "DHT22";
        case SENSOR_TYPE_CUSTOM:  return "CUSTOM";
        default:                  return "???";
    }
}

static void draw_entry(int y, const display_entry_t *entry) {
    char line[42];
    
    // Format: "[LoRa] S01 BME280  -87dBm +2.3dB"
    int len = snprintf(line, sizeof(line), "[%s] S%02d %s",
                       source_to_string(entry->source),
                       entry->sensor_nr,
                       sensor_type_to_string(entry->sensor_type));
    
    // Add sensor data
    if (entry->source == SENSOR_SOURCE_LORA) {
        // Show SNR and RSSI for LoRa packets
        if (entry->rssi >= 0) {
            len += snprintf(line + len, sizeof(line) - len, "  %ddBm", entry->rssi);
        }
        if (entry->snr >= -40.0f && entry->snr <= 40.0f) {
            len += snprintf(line + len, sizeof(line) - len, "  %+.1fdB", (double)entry->snr);
        }
    } else {
        len += snprintf(line + len, sizeof(line) - len, "  ESPN");
    }
    
    u8g2_DrawStr(g_u8g2, 2, y, line);
}

esp_err_t display_driver_init(u8g2_t *u8g2) {
    g_u8g2 = u8g2;
    g_display_mutex = xSemaphoreCreateMutex();
    if (g_display_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return ESP_ERR_NO_MEM;
    }
    g_buffer_count = 0;
    g_lora_count = 0;
    g_espnow_count = 0;
    g_dropped = 0;
    memset(g_display_buffer, 0, sizeof(g_display_buffer));
    
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
    
    // Add new entry to buffer
    uint8_t new_idx = g_buffer_count % DISPLAY_BUFFER_SIZE;
    g_display_buffer[new_idx].source = (packet->msg_type == SENSOR_SOURCE_LORA) ? 
                                        SENSOR_SOURCE_LORA : SENSOR_SOURCE_ESPNOW;
    g_display_buffer[new_idx].sensor_nr = packet->sensor_nr;
    g_display_buffer[new_idx].sensor_type = packet->sensor_type;
    g_display_buffer[new_idx].rssi = packet->lora_rssi;
    g_display_buffer[new_idx].snr = packet->lora_snr;
    g_display_buffer[new_idx].timestamp = packet->timestamp;
    
    if (g_buffer_count < DISPLAY_BUFFER_SIZE) {
        g_buffer_count++;
    }
    
    // Update counters
    if (g_display_buffer[new_idx].source == SENSOR_SOURCE_LORA) {
        g_lora_count++;
    } else {
        g_espnow_count++;
    }
    
    // Draw display
    u8g2_ClearBuffer(g_u8g2);
    u8g2_SetFont(g_u8g2, DISPLAY_FONT);
    
    // Header
    u8g2_DrawStr(g_u8g2, 2, DISPLAY_HEADER_Y, "Sensor Receiver");
    
    // Draw entries (newest at top, scrolling up)
    int visible_count = g_buffer_count < DISPLAY_ENTRY_LINES ? 
                         g_buffer_count : DISPLAY_ENTRY_LINES;
    int start_idx = (g_buffer_count - 1) % DISPLAY_BUFFER_SIZE;
    
    for (int i = 0; i < visible_count; i++) {
        int idx = (start_idx - i + DISPLAY_BUFFER_SIZE) % DISPLAY_BUFFER_SIZE;
        int y = DISPLAY_ENTRY_START_Y + (i * DISPLAY_LINE_HEIGHT);
        draw_entry(y, &g_display_buffer[idx]);
    }
    
    // Status line
    char status[42];
    snprintf(status, sizeof(status), "L:%lu E:%lu D:%lu", 
             g_lora_count, g_espnow_count, g_dropped);
    u8g2_DrawStr(g_u8g2, 2, DISPLAY_STATUS_Y, status);
    
    u8g2_SendBuffer(g_u8g2);
    
    xSemaphoreGive(g_display_mutex);
    return ESP_OK;
}

void display_driver_show_welcome(u8g2_t *u8g2) {
    if (u8g2 == NULL) return;
    
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, DISPLAY_FONT);
    u8g2_DrawStr(u8g2, 2, 20, "Sensor Receiver");
    u8g2_DrawStr(u8g2, 2, 30, "Heltec ESP32 S3");
    u8g2_DrawStr(u8g2, 2, 40, "LoRa + ESP-NOW");
    u8g2_DrawStr(u8g2, 2, 50, "Starting...");
    u8g2_SendBuffer(u8g2);
}

void display_driver_show_error(u8g2_t *u8g2, const char *error_msg) {
    if (u8g2 == NULL) return;
    
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, DISPLAY_FONT);
    u8g2_DrawStr(u8g2, 2, 25, "ERROR:");
    u8g2_DrawStr(u8g2, 2, 37, error_msg);
    u8g2_DrawStr(u8g2, 2, 49, "Check logs!");
    u8g2_SendBuffer(u8g2);
}

void display_driver_show_status(u8g2_t *u8g2, uint32_t lora_count, uint32_t espnow_count, uint32_t dropped) {
    if (u8g2 == NULL || g_display_mutex == NULL) return;
    
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    g_lora_count = lora_count;
    g_espnow_count = espnow_count;
    g_dropped = dropped;
    
    // Only update status line if buffer exists
    if (g_buffer_count > 0) {
        char status[42];
        snprintf(status, sizeof(status), "L:%lu E:%lu D:%lu", 
                 g_lora_count, g_espnow_count, g_dropped);
        
        u8g2_SetDrawColor(u8g2, 1);
        u8g2_DrawBox(u8g2, 0, DISPLAY_STATUS_Y - 10, 128, 12);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, 2, DISPLAY_STATUS_Y, status);
        u8g2_SetDrawColor(u8g2, 1);
    }
    
    u8g2_SendBuffer(u8g2);
    xSemaphoreGive(g_display_mutex);
}
