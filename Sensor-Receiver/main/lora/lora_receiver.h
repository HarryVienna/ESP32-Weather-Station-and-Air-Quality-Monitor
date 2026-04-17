#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include "esp_err.h"
#include "sx1262.h"
#include "sensor_stack.h"

/* ============================================================================
 * LoRa Receiver Configuration
 * ============================================================================ */

#define LORA_FREQUENCY          869525000      // 869,525 MHz == Middle of G3 band
#define LORA_BANDWIDTH          LORA_BW_125
#define LORA_SPREADING_FACTOR   10             // SF10 (balanced range/sensitivity)
#define LORA_CODING_RATE        LORA_CR_4_5
#define LORA_TX_POWER           12             // dBm (RX mode, lower power OK)
#define LORA_PREAMBLE_LENGTH    8
#define LORA_PAYLOAD_LENGTH     (sizeof(packet_header_t) + sizeof(link_metadata_t) + MAX_PAYLOAD_SIZE)
#define LORA_CRC_ON             true
#define LORA_IQ_INVERTED        true
#define LORA_RX_GAIN_BOOSTED    true

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize the LoRa receiver state
 * 
 * @return ESP_OK on success
 */
esp_err_t lora_receiver_init(void);

/**
 * @brief Configure and start LoRa reception
 * 
 * Configures the SX1262 with the specified parameters and starts
 * async receive mode. The callback is called for each received packet.
 * 
 * @return ESP_OK on success
 */
esp_err_t lora_receiver_start(void);

/**
 * @brief Stop LoRa reception
 * 
 * Puts SX1262 into sleep mode.
 */
void lora_receiver_stop(void);

/**
 * @brief Get LoRa receive statistics
 * @param packets_received Output: total packets received
 * @param crc_errors Output: CRC error count
 */
void lora_receiver_stats(uint32_t *packets_received, uint32_t *crc_errors);

#endif /* LORA_RECEIVER_H */
