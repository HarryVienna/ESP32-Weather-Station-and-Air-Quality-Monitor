#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_TASK_STATUS_IDLE        = 0,
    OTA_TASK_STATUS_IN_PROGRESS = 1,
    OTA_TASK_STATUS_FAILED      = 2,
} ota_task_status_t;

/* Called from i2c_slave.c when the P4 writes I2C_REG_SET_WIFI_SSID /
 * I2C_REG_SET_WIFI_PASS. Kept in RAM only, never persisted to NVS - the P4
 * sends them fresh before every I2C_REG_OTA_START, taken from its own
 * setup-screen NVS entry (see
 * BaseStation/main/gui/setup/gui_setup_network_actions.c). */
void ota_task_set_wifi_ssid(const char *ssid);
void ota_task_set_wifi_password(const char *password);

/* Called from i2c_slave.c when the P4 writes I2C_REG_OTA_START. Starts the
 * update in its own FreeRTOS task (the I2C slave task must stay
 * responsive) - pauses ESP-NOW, connects to the AP configured via
 * ota_task_set_wifi_*(), downloads the "Receiver.bin" asset of the latest
 * GitHub release and flashes it. Reboots on success; on failure falls back
 * to normal ESP-NOW operation (see esp_now_resume() in network/esp-now.h).
 * Returns immediately. Ignored (with a warning) if an update is already in
 * progress. */
void ota_task_trigger(void);

#ifdef __cplusplus
}
#endif
