#ifndef GUI_WEATHER_H
#define GUI_WEATHER_H

#include "weather/weather_data.h"

/* Wettervorhersage (Open-Meteo) - aktuell/stuendlich/taeglich */

void disp_weather(current_weather_data_t *current_weather, hourly_weather_data_t *hourly_weather, daily_weather_data_t *daily_weather);

/* Ersetzt den Hourly/Daily-Chart-Platzhalter aus EEZ Studio
 * (objects.hourly_chart / objects.daily_chart) durch die echten
 * lv_hourly_chart_t/lv_daily_chart_t-Widgets an gleicher Position/Groesse.
 * Von main.c einmal nach create_screens() aufgerufen (siehe ui_init()) -
 * dadurch bleibt screens.c komplett generiert, ein erneuter
 * EEZ-Studio-Export verliert diesen Schritt nie. Unabhaengig von
 * gui_sen66_init_charts() (siehe dort). */
void gui_weather_init_charts(void);

/* Startet weather_task (Open-Meteo-Abruf) - von gui_actions.c aufgerufen,
 * sobald WLAN steht. */
void gui_weather_start_task(void);

#endif /* GUI_WEATHER_H */
