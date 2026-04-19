#include "lora.h"
#include "sensor_stack.h"
#include "display_driver.h"
#include "esp_log.h"
#include "sx1262.h"

static const char* TAG = "lora_rx";

/* ============================================================================
 * SX1262 Receive Callback
 * ============================================================================ */

static void lora_rx_callback(uint8_t *data, uint8_t len, sx1262_packet_status_t *status) {
    if (data == NULL || len == 0 || status == NULL) {
        return;
    }
    
    // Minimum size: header (4) + link metadata (12) = 16 bytes, no payload
    if (len < sizeof(packet_header_t) + sizeof(link_metadata_t)) {
        ESP_LOGW(TAG, "Packet too small: expected >= %zu, got %d", 
                 sizeof(packet_header_t) + sizeof(link_metadata_t), len);
        return;
    }
    
    // Parse header to get payload length
    packet_header_t *header = (packet_header_t *)data;
    uint8_t expected_total = sizeof(packet_header_t) + sizeof(link_metadata_t) + header->payload_len;
    
    if (len != expected_total) {
        ESP_LOGW(TAG, "Packet size mismatch: expected %d, got %d", expected_total, len);
        return;
    }
    
    if (header->payload_len > MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "Payload too large: %d bytes (max %d)", 
                 header->payload_len, MAX_PAYLOAD_SIZE);
        return;
    }
    
    // Build sensor_packet_t from received data
    sensor_packet_t packet;
    
    // Copy header
    memcpy(&packet.header, data, sizeof(packet_header_t));
    
    // Copy link metadata (starts after header)
    memcpy(&packet.link, data + sizeof(packet_header_t), sizeof(link_metadata_t));
    
    // Add LoRa-specific metadata (override any sender values)
    packet.link.msg_source = SENSOR_SOURCE_LORA;
    packet.link.lora_rssi = status->rssi_pkt;
    packet.link.lora_snr = status->snr_pkt;
    packet.link.timestamp = xTaskGetTickCount();
    
    // Copy payload (starts after header + link metadata)
    uint8_t *payload_start = data + sizeof(packet_header_t) + sizeof(link_metadata_t);
    memcpy(packet.payload, payload_start, header->payload_len);
    
    // Push to stack
    esp_err_t ret = sensor_stack_push(&packet, SENSOR_SOURCE_LORA);
    if (ret == ESP_OK) {
        // Update display (only needs header + link metadata)
        display_driver_update(&packet);
        
        ESP_LOGI(TAG, "LoRa RX: Sensor %d [Type=%d] payload=%d bytes, RSSI:%d, SNR:%.1f",
                 packet.header.sensor_nr,
                 packet.header.sensor_type,
                 packet.header.payload_len,
                 status->rssi_pkt, status->snr_pkt);
    } else {
        ESP_LOGW(TAG, "Stack full, LoRa packet dropped");
    }
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

esp_err_t init_lora(void) {
    ESP_LOGI(TAG, "Initializing LoRa (SX1262)...");
    
    // Phase 1: Initialize SPI bus and GPIOs
    esp_err_t ret = sx1262_init_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase 2: Cold start radio
    ret = sx1262_init_radio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa radio: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LoRa hardware initialized");
    return ESP_OK;
}

esp_err_t lora_start(void) {
    sx1262_config_t config = {
        .modem_mode = SX1262_MODEM_LORA,
        .frequency = LORA_FREQUENCY,
        .tx_power = LORA_TX_POWER,
        .bandwidth = LORA_BANDWIDTH,
        .spreading_factor = LORA_SPREADING_FACTOR,
        .coding_rate = LORA_CODING_RATE,
        .preamble_length = LORA_PREAMBLE_LENGTH,
        .payload_length = LORA_PAYLOAD_LENGTH,
        .crc_on = LORA_CRC_ON,
        .iq_inverted = LORA_IQ_INVERTED,
        .rx_gain_boosted = LORA_RX_GAIN_BOOSTED,
        .sync_word = 0x1424,  // Public LoRa network
    };
    
    ESP_LOGI(TAG, "Configuring LoRa: %lu MHz, SF%d, BW%d",
             config.frequency / 1000000, config.spreading_factor,
             (config.bandwidth == LORA_BW_125) ? 125 :
             (config.bandwidth == LORA_BW_250) ? 250 : 500);
    
    esp_err_t ret = sx1262_configure(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LoRa: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start async receive with callback
    ret = sx1262_start_receive_async(lora_rx_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start async RX: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LoRa receiver started");
    return ESP_OK;
}

void lora_stop(void) {
    sx1262_sleep();
    ESP_LOGI(TAG, "LoRa receiver stopped");
}
