#ifndef OPEN_METEO_PROVIDER_H
#define OPEN_METEO_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "weather_http.h"

#ifdef __cplusplus
extern "C" {
#endif

bool open_meteo_fetch_current(esp_http_client_handle_t client, weather_http_response_t *response,
                               const char *latitude, const char *longitude, current_weather_data_t *out);

bool open_meteo_fetch_hourly(esp_http_client_handle_t client, weather_http_response_t *response,
                              const char *latitude, const char *longitude, hourly_weather_data_t *out, int count);

bool open_meteo_fetch_daily(esp_http_client_handle_t client, weather_http_response_t *response,
                             const char *latitude, const char *longitude, daily_weather_data_t *out, int count);

#ifdef __cplusplus
}
#endif

#endif /* OPEN_METEO_PROVIDER_H */
