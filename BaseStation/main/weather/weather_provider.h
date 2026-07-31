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

/* Fetches current+hourly+daily for the selected provider. `api_key` is
 * only used by OpenWeatherMap/VisualCrossing, ignored for Open-Meteo (pass
 * NULL/"" there). Open-Meteo and OWM internally need several HTTP calls
 * for this (see *_provider.c), VisualCrossing manages with a single call -
 * that's hidden behind this one function, weather_task.c doesn't need to
 * know. Returns false as soon as one of the three parts fails. */
bool weather_provider_fetch_all(weather_provider_t provider, esp_http_client_handle_t client,
                                 weather_http_response_t *response, const char *latitude, const char *longitude,
                                 const char *api_key, current_weather_data_t *current_out,
                                 hourly_weather_data_t *hourly_out, int hourly_count,
                                 daily_weather_data_t *daily_out, int daily_count);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_PROVIDER_H */
