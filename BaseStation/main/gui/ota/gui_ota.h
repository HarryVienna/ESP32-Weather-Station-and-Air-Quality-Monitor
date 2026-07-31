#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Zeigt die "Update verfuegbar"-MessageBox (message_box_update) auf
 *         dem Weatherstation-Screen an und traegt die Versionsnummer ins
 *         Label update_version ein. Von ota_task.c aufgerufen, sobald ein
 *         neueres Release gefunden wurde - der eigentliche Download startet
 *         erst, wenn der Nutzer im Dialog auf "Installieren" klickt (siehe
 *         action_event_message_box_update() in gui_ota.c).
 *
 * @param  version  Versionsnummer des Updates (z.B. "v0.2.0"), wird kopiert -
 *                   muss nur fuer die Dauer des Aufrufs gueltig sein.
 */
void gui_ota_update_available(const char *version);

/**
 * @brief  Setzt den Fortschrittsbalken (message_box_update/progress_bar_update)
 *         auf 0. Von ota_task.c aufgerufen, bevor der Firmware-Download beginnt.
 */
void gui_ota_update_started(void);

/**
 * @brief  Aktualisiert den Fortschrittsbalken in der MessageBox.
 * @param  percent  0-100
 */
void gui_ota_update_progress(int32_t percent);

/**
 * @brief  Blendet die MessageBox wieder aus, weil das Update fehlgeschlagen
 *         ist (sonst bliebe der Fortschrittsbalken sichtbar haengen).
 */
void gui_ota_update_failed(void);

#ifdef __cplusplus
}
#endif
