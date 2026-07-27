#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA-Update-Check als FreeRTOS-Task starten.
 *
 * Prueft periodisch das "latest" GitHub Release des Projekts gegen die
 * aktuell laufende Firmware-Version und spielt ein neueres Release per
 * esp_https_ota() ein (mit anschliessendem Reboot). Muss erst gestartet
 * werden, wenn WLAN garantiert verbunden ist. Kehrt sofort zurueck.
 */
void ota_task_start(void);

#ifdef __cplusplus
}
#endif
