#ifndef GUI_WEATHER_H
#define GUI_WEATHER_H

#include "weather/weather_data.h"

/* Weather forecast (Open-Meteo) - current/hourly/daily */

void disp_weather(current_weather_data_t *current_weather, hourly_weather_data_t *hourly_weather, daily_weather_data_t *daily_weather);

/* Replaces the hourly/daily chart placeholder from EEZ Studio
 * (objects.hourly_chart / objects.daily_chart) with the real
 * lv_hourly_chart_t/lv_daily_chart_t widgets at the same position/size.
 * Called once by main.c after create_screens() (see ui_init()) - this way
 * screens.c stays fully generated, and a re-export from EEZ Studio never
 * loses this step. Independent of gui_sen66_init_charts() (see there). */
void gui_weather_init_charts(void);

/* Starts weather_task (Open-Meteo fetch) - called by
 * gui_weatherstation_screen_actions.c once WiFi is up. */
void gui_weather_start_task(void);

#endif /* GUI_WEATHER_H */
