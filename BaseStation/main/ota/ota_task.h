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

/**
 * @brief Startet den Download/Flash des zuletzt gefundenen Updates.
 *
 * Wird vom "Installieren"-Button der Update-verfuegbar-MessageBox aufgerufen
 * (siehe gui/ota/gui_ota.c: action_event_weatherstation_update_pressed()). Startet
 * einen eigenen Task fuer den (blockierenden) Download, damit der LVGL-Task,
 * von dem der Button-Klick kommt, nicht blockiert. Kehrt sofort zurueck.
 */
void ota_task_install_available_update(void);

#ifdef __cplusplus
}
#endif
