#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the OTA update check as a FreeRTOS task.
 *
 * Periodically checks the project's "latest" GitHub release against the
 * currently running firmware version and installs a newer release via
 * esp_https_ota() (followed by a reboot). As soon as the P4 updates
 * itself, the ESP32-S3 receiver ALWAYS gets an update triggered in the
 * same step (see receiver/receiver.h: receiver_start_ota()) - independent
 * of any version check of its own for the receiver. The P4 only sends it
 * the WiFi SSID/password and the start command via I2C; the receiver
 * downloads its own "Receiver.bin" from the same release itself. Must
 * only be started once WiFi is guaranteed to be connected. Returns
 * immediately.
 */
void ota_task_start(void);

/**
 * @brief Starts the download/flash of the most recently found update.
 *
 * Called by the "Install" button of the update-available message box (see
 * gui/ota/gui_ota.c: action_event_weatherstation_update_pressed()). Starts
 * its own task for the (blocking) download, so the LVGL task the button
 * click comes from doesn't block. Returns immediately.
 */
void ota_task_install_available_update(void);

#ifdef __cplusplus
}
#endif
