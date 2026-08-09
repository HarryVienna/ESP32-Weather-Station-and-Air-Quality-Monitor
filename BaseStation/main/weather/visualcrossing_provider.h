#ifndef VISUALCROSSING_PROVIDER_H
#define VISUALCROSSING_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "http/http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unlike Open-Meteo/OWM, the VisualCrossing Timeline API delivers current+
 * hourly+daily in a single call (include=days,hours,current) - hence one
 * combined function here instead of three separate fetches. */
bool visualcrossing_fetch_all(esp_http_client_handle_t client, http_response_t *response,
                               const char *latitude, const char *longitude, const char *api_key,
                               current_weather_data_t *current_out, hourly_weather_data_t *hourly_out,
                               int hourly_count, daily_weather_data_t *daily_out, int daily_count);

#ifdef __cplusplus
}
#endif

#endif /* VISUALCROSSING_PROVIDER_H */
