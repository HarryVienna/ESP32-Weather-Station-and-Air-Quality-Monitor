#ifndef OPEN_METEO_PROVIDER_H
#define OPEN_METEO_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "http/http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

bool open_meteo_fetch_current(esp_http_client_handle_t client, http_response_t *response,
                               const char *latitude, const char *longitude, current_weather_data_t *out);

bool open_meteo_fetch_hourly(esp_http_client_handle_t client, http_response_t *response,
                              const char *latitude, const char *longitude, hourly_weather_data_t *out, int count);

/* Open-Meteo has no sunrise/sunset in its "current" block (only "daily"
 * has it) - current_out's sunrise/sunset are filled in here from day 0
 * alongside the rest of the daily data, so gui_weather.c can read them
 * uniformly from current_weather_data_t regardless of provider. */
bool open_meteo_fetch_daily(esp_http_client_handle_t client, http_response_t *response,
                             const char *latitude, const char *longitude, daily_weather_data_t *out, int count,
                             current_weather_data_t *current_out);

#ifdef __cplusplus
}
#endif

#endif /* OPEN_METEO_PROVIDER_H */
