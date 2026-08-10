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
 * Gets the I2C bus from i2c_manager and adds the S3 slave device. Must run
 * before wifi_init(receiver_sync_time) in main.c - see receiver_sync_time()
 * for why time/timezone aren't pushed here yet.
 *
 * Must be called after i2c_manager_init().
 *
 * @return ESP_OK on success
 */
esp_err_t receiver_init(void);

/**
 * @brief Pushes the current timezone + system time to the S3 over I2C.
 *
 * Timezone is read fresh from NVS ("weatherstation"/"tz", see
 * gui/setup/gui_setup_screen_actions.c: save_region_and_timezone()) on
 * every call - so re-running this after the user changes it in the setup
 * screen picks it up automatically, no separate "timezone changed" hook
 * needed. Does nothing if the system clock isn't synchronized yet (always
 * true before the first SNTP sync, e.g. right after receiver_init() -
 * WiFi/SNTP haven't even started at that point).
 *
 * Passed to wifi_init() as its time_synced_cb in main.c, so this runs
 * once per SNTP sync (initial and every periodic resync) - the S3's clock
 * stays correct for the device's entire uptime, not just at boot.
 */
void receiver_sync_time(void);

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
