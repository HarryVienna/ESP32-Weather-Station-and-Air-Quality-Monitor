#ifndef GUI_STATUS_H
#define GUI_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* Weatherstation screen: general status (WiFi, time, brightness) */

void disp_wifi_status(bool status, int8_t rssi_dbm);
void disp_date_time(char *date_time);
void set_brightness(uint16_t brightness);

/* Starts clock_task (time/date) - called by
 * gui_weatherstation_screen_actions.c once WiFi/NTP are up (clock_task
 * would otherwise show an unsynchronized time). */
void gui_status_start_clock_task(void);

/* Starts brightness_task (brightness/presence sensor) - technically
 * independent of WiFi (pure I2C sensor), but still only starts together
 * with clock_task/weather_task/gui_sen66_start_task in
 * gui_weatherstation_screen_actions.c, not already in main.c - see the
 * comment there for the trade-off. */
void gui_status_start_brightness_task(void);

#endif /* GUI_STATUS_H */
