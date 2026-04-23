#ifndef LORA_H
#define LORA_H

#include "esp_err.h"
#include "sx1262.h"
#include "sensor_stack.h"

/* ============================================================================
 * LoRa Receiver Configuration
 * ============================================================================ */

#define LORA_FREQUENCY          869525000      // 869,525 MHz == Middle of G3 band
#define LORA_BANDWIDTH          LORA_BW_125
#define LORA_SPREADING_FACTOR   7
#define LORA_CODING_RATE        LORA_CR_4_5
#define LORA_TX_POWER           12             // RX Mode, ignored
#define LORA_PREAMBLE_LENGTH    8
#define LORA_PAYLOAD_LENGTH     0
#define LORA_CRC_ON             true
#define LORA_IQ_INVERTED        true
#define LORA_RX_GAIN_BOOSTED    true
#define LORA_SYNC_WORD          0x1424         // Public LoRa network

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize LoRa hardware (SPI bus, GPIO, radio)
 * 
 * @return ESP_OK on success
 */
esp_err_t init_lora(void);

/**
 * @brief Configure and start LoRa reception
 * 
 * Configures the SX1262 with the specified parameters and starts
 * async receive mode. The callback is called for each received packet.
 * 
 * @return ESP_OK on success
 */
esp_err_t lora_start(void);

/**
 * @brief Stop LoRa reception
 * 
 * Puts SX1262 into sleep mode.
 */
void lora_stop(void);

#endif /* LORA_H */
