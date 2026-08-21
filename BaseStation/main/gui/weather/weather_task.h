#ifndef WEATHER_TASK_H
#define WEATHER_TASK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void weather_task(void *pvParameter);

/* Shared with the setup screen (see gui_setup.c: gui_setup_refresh_start_button())
 * so "Start" only enables once the coordinates are actually usable, instead of
 * navigating to the Weatherstation screen and only then discovering weather_task()
 * refuses to start (see the "Invalid coordinates from NVS" log path below). */
bool validate_coordinates(const char* lat, const char* lon);

#ifdef __cplusplus
}
#endif

#endif