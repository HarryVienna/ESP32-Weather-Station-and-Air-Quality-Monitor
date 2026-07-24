#ifndef WEATHER_PROVIDER_H
#define WEATHER_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "weather_http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_PROVIDER_OPEN_METEO = 0,
    WEATHER_PROVIDER_OPENWEATHERMAP = 1,
    WEATHER_PROVIDER_VISUALCROSSING = 2,
} weather_provider_t;

/* Holt current+hourly+daily fuer den gewaehlten Provider. `api_key` wird nur
 * von OpenWeatherMap/VisualCrossing benutzt, bei Open-Meteo ignoriert (dort
 * NULL/"" uebergeben). Open-Meteo und OWM brauchen dafuer intern mehrere
 * HTTP-Calls (siehe *_provider.c), VisualCrossing kommt mit einem einzigen
 * Call aus - das ist hinter dieser einen Funktion versteckt, weather_task.c
 * muss das nicht wissen. Rueckgabe false, sobald einer der drei Teile
 * fehlschlaegt. */
bool weather_provider_fetch_all(weather_provider_t provider, esp_http_client_handle_t client,
                                 weather_http_response_t *response, const char *latitude, const char *longitude,
                                 const char *api_key, current_weather_data_t *current_out,
                                 hourly_weather_data_t *hourly_out, int hourly_count,
                                 daily_weather_data_t *daily_out, int daily_count);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_PROVIDER_H */
