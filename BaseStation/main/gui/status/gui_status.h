#ifndef GUI_STATUS_H
#define GUI_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* Weatherstation-Screen: allgemeiner Status (WLAN, Uhrzeit, Helligkeit) */

void disp_wifi_status(bool status, int8_t rssi_dbm);
void disp_date_time(char *date_time);
void set_brightness(uint16_t brightness);

/* Startet clock_task (Uhrzeit/Datum) - von gui_actions.c aufgerufen, sobald
 * WLAN/NTP steht (clock_task zeigt sonst eine nicht synchronisierte Uhrzeit). */
void gui_status_start_clock_task(void);

/* Startet brightness_task (Helligkeits-/Anwesenheitssensor) - unabhaengig
 * von WLAN, deshalb separat und von main.c direkt beim Boot aufgerufen statt
 * ueber gui_status_start_task(). */
void gui_status_start_brightness_task(void);

#endif /* GUI_STATUS_H */
