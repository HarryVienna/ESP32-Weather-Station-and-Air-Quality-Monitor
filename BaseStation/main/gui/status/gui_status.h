#ifndef GUI_STATUS_H
#define GUI_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* Weatherstation-Screen: allgemeiner Status (WLAN, Uhrzeit, Helligkeit) */

void disp_wifi_status(bool status, int8_t rssi_dbm);
void disp_date_time(char *date_time);
void set_brightness(uint16_t brightness);

/* Startet clock_task (Uhrzeit/Datum) - von gui_weatherstation_screen_actions.c
 * aufgerufen, sobald WLAN/NTP steht (clock_task zeigt sonst eine nicht
 * synchronisierte Uhrzeit). */
void gui_status_start_clock_task(void);

/* Startet brightness_task (Helligkeits-/Anwesenheitssensor) - technisch
 * unabhaengig von WLAN (reiner I2C-Sensor), startet aber trotzdem erst mit
 * clock_task/weather_task/gui_sen66_start_task zusammen in
 * gui_weatherstation_screen_actions.c, nicht schon in main.c - siehe
 * Kommentar dort fuer die Abwaegung. */
void gui_status_start_brightness_task(void);

#endif /* GUI_STATUS_H */
