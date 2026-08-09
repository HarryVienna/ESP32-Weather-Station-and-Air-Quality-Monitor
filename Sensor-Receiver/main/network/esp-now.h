#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
esp_err_t init_wifi(void);
esp_err_t esp_now_start(void);

/* Stops ESP-NOW so the WiFi radio is free to associate with a real AP (for
 * the OTA download in ota/ota_task.c). Leaves the WiFi driver itself
 * running in STA mode - only esp_now_deinit(), no esp_wifi_stop(). */
esp_err_t esp_now_pause(void);

/* Reverses esp_now_pause(): re-pins the fixed ESP-NOW channel (a STA
 * association to an AP leaves the radio parked on the AP's channel) and
 * re-initializes ESP-NOW. Must be called after esp_wifi_disconnect() from
 * the AP - either once the OTA download failed (ota_task.c falls back to
 * normal operation) or, on success, implicitly via the reboot that follows
 * a working update (esp_now_start() then runs fresh from app_main()). */
esp_err_t esp_now_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
