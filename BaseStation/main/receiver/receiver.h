#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the sensor receiver.
 *
 * Gets the I2C bus from i2c_manager, adds the S3 slave device, and
 * transfers timezone + current system time once.
 *
 * Must be called after i2c_manager_init().
 *
 * @return ESP_OK on success
 */
esp_err_t receiver_init(void);

/**
 * @brief Start sensor polling as a FreeRTOS task.
 *
 * Creates a task that fetches available packets from the S3 slave every
 * 2s and logs them via ESP_LOGI. Returns immediately.
 */
void receiver_start(void);

/**
 * @brief Starts a receiver firmware update via I2C.
 *
 * Sends SSID + password to the S3 (kept there in RAM only, never in NVS -
 * see Sensor-Receiver/main/ota/ota_task.h) and then triggers the update.
 * Fire-and-forget: the S3 pauses ESP-NOW, connects to the AP, downloads
 * "Receiver.bin" from the latest GitHub release and reboots on success.
 * On failure it falls back to ESP-NOW operation on its own.
 *
 * @param ssid      WiFi SSID (max. 32 characters)
 * @param password  WiFi password (max. 63 characters)
 */
esp_err_t receiver_start_ota(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
