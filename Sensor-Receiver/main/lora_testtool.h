/**
 * @file lora_testtool.h
 * @brief LoRa Test Tool Header
 */

#ifndef LORA_TESTTOOL_H
#define LORA_TESTTOOL_H

#include "esp_err.h"
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the LoRa TestTool
 * * Prerequisites:
 * - U8g2 display must already be initialized
 * - SPI must NOT yet be initialized (is done internally)
 * * @param display Pointer to initialized u8g2 display
 * @return ESP_OK on success, ESP_FAIL on error
 * * @note After successful initialization, the tool runs independently
 */
esp_err_t lora_testtool_init(u8g2_t* display);

/**
 * @brief Frees all resources
 */
void lora_testtool_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_TESTTOOL_H