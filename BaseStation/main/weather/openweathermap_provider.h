#ifndef OPENWEATHERMAP_PROVIDER_H
#define OPENWEATHERMAP_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "weather_http.h"

#ifdef __cplusplus
extern "C" {
#endif

bool openweathermap_fetch_current(esp_http_client_handle_t client, weather_http_response_t *response,
                                   const char *latitude, const char *longitude, const char *api_key,
                                   current_weather_data_t *out);

bool openweathermap_fetch_hourly(esp_http_client_handle_t client, weather_http_response_t *response,
                                  const char *latitude, const char *longitude, const char *api_key,
                                  hourly_weather_data_t *out, int count);

bool openweathermap_fetch_daily(esp_http_client_handle_t client, weather_http_response_t *response,
                                 const char *latitude, const char *longitude, const char *api_key,
                                 daily_weather_data_t *out, int count);

#ifdef __cplusplus
}
#endif

#endif /* OPENWEATHERMAP_PROVIDER_H */
