#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Wechselt auf den Update-Screen und setzt den Fortschrittsbalken auf 0.
 *         Von ota_task.c aufgerufen, bevor der Firmware-Download beginnt.
 */
void gui_ota_update_started(void);

/**
 * @brief  Aktualisiert den Fortschrittsbalken des Update-Screens.
 * @param  percent  0-100
 */
void gui_ota_update_progress(int32_t percent);

/**
 * @brief  Wechselt zurueck auf den Weatherstation-Screen, weil das Update
 *         fehlgeschlagen ist (sonst bliebe das Geraet auf "Upgrading..." haengen).
 */
void gui_ota_update_failed(void);

#ifdef __cplusplus
}
#endif
