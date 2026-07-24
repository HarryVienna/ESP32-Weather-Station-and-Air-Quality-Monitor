#ifndef VISUALCROSSING_PROVIDER_H
#define VISUALCROSSING_PROVIDER_H

#include <stdbool.h>

#include "weather_data.h"
#include "weather_http.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Anders als Open-Meteo/OWM liefert die VisualCrossing Timeline-API current+
 * hourly+daily in einem einzigen Call (include=days,hours,current) - daher
 * hier eine kombinierte Funktion statt drei getrennter Fetches. */
bool visualcrossing_fetch_all(esp_http_client_handle_t client, weather_http_response_t *response,
                               const char *latitude, const char *longitude, const char *api_key,
                               current_weather_data_t *current_out, hourly_weather_data_t *hourly_out,
                               int hourly_count, daily_weather_data_t *daily_out, int daily_count);

#ifdef __cplusplus
}
#endif

#endif /* VISUALCROSSING_PROVIDER_H */
