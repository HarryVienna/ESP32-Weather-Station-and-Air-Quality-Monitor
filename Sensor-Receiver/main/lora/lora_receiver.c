#include "lora_receiver.h"
#include "sensor_stack.h"
#include "display_driver.h"
#include "esp_log.h"

static const char* TAG = "lora_rx";

static volatile uint32_t g_packets_received = 0;
static volatile uint32_t g_crc_errors = 0;

/* ============================================================================
 * Helper: sensor_type_to_string
 * ============================================================================ */

static const char* sensor_type_to_string(uint8_t type) {
    switch (type) {
        case SENSOR_TYPE_BME280:  return "BME280";
        case SENSOR_TYPE_HDC1080: return "HDC1080";
        case SENSOR_TYPE_DHT22:   return "DHT22";
        case SENSOR_TYPE_CUSTOM:  return "CUSTOM";
        default:                  return "???";
    }
}

/* ============================================================================
 * SX1262 Receive Callback
 * ============================================================================ */

static void lora_rx_callback(uint8_t *data, uint8_t len, sx1262_packet_status_t *status) {
    if (data == NULL || len == 0 || status == NULL) {
        return;
    }
    
    if (len != sizeof(sensor_packet_t)) {
        ESP_LOGW(TAG, "Packet size mismatch: expected %zu, got %d", 
                 sizeof(sensor_packet_t), len);
        return;
    }
    
    // Copy packet and add LoRa metadata
    sensor_packet_t packet;
    memcpy(&packet, data, sizeof(sensor_packet_t));
    packet.msg_type = SENSOR_SOURCE_LORA;
    packet.lora_rssi = status->rssi_pkt;
    packet.lora_snr = status->snr_pkt;
    packet.timestamp = xTaskGetTickCount();
    
    // Push to stack
    esp_err_t ret = sensor_stack_push(&packet, SENSOR_SOURCE_LORA);
    if (ret == ESP_OK) {
        g_packets_received++;
        
        // Update display
        display_driver_update(&packet);
        
        // Log every 10th packet
        if (g_packets_received % 10 == 0) {
            ESP_LOGI(TAG, "LoRa RX #%lu: Sensor %d [%s], RSSI:%d, SNR:%.1f, Stack:%d",
                     g_packets_received, packet.sensor_nr,
                     sensor_type_to_string(packet.sensor_type),
                     status->rssi_pkt, status->snr_pkt,
                     sensor_stack_count());
        }
    } else {
        ESP_LOGW(TAG, "Stack full, LoRa packet dropped");
    }
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

esp_err_t lora_receiver_init(void) {
    g_packets_received = 0;
    g_crc_errors = 0;
    return ESP_OK;
}

esp_err_t lora_receiver_start(void) {
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
    
    // Start continuous receive in async mode
    ret = sx1262_start_receive_async(lora_rx_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start RX: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LoRa RX started (SF%d, BW%d)", 
             config.spreading_factor, config.bandwidth);
    
    return ESP_OK;
}

void lora_receiver_stop(void) {
    sx1262_stop_receive_async();
    sx1262_sleep();
    ESP_LOGI(TAG, "LoRa RX stopped");
}

void lora_receiver_stats(uint32_t *packets_received, uint32_t *crc_errors) {
    if (packets_received) *packets_received = g_packets_received;
    if (crc_errors) *crc_errors = g_crc_errors;
}
