#include "weather_provider.h"
#include "open_meteo_provider.h"
#include "openweathermap_provider.h"
#include "visualcrossing_provider.h"

bool weather_provider_fetch_all(weather_provider_t provider, esp_http_client_handle_t client,
                                 http_response_t *response, const char *latitude, const char *longitude,
                                 const char *api_key, current_weather_data_t *current_out,
                                 hourly_weather_data_t *hourly_out, int hourly_count,
                                 daily_weather_data_t *daily_out, int daily_count) {
    switch (provider) {
        case WEATHER_PROVIDER_OPENWEATHERMAP: {
            bool current_ok = openweathermap_fetch_current(client, response, latitude, longitude, api_key, current_out);
            bool hourly_ok = openweathermap_fetch_hourly(client, response, latitude, longitude, api_key, hourly_out, hourly_count);
            bool daily_ok = openweathermap_fetch_daily(client, response, latitude, longitude, api_key, daily_out, daily_count);
            return current_ok && hourly_ok && daily_ok;
        }
        case WEATHER_PROVIDER_VISUALCROSSING:
            // Ein einziger Call liefert alles drei auf einmal (siehe visualcrossing_provider.c).
            return visualcrossing_fetch_all(client, response, latitude, longitude, api_key, current_out, hourly_out,
                                             hourly_count, daily_out, daily_count);
        case WEATHER_PROVIDER_OPEN_METEO:
        default: {
            bool current_ok = open_meteo_fetch_current(client, response, latitude, longitude, current_out);
            bool hourly_ok = open_meteo_fetch_hourly(client, response, latitude, longitude, hourly_out, hourly_count);
            bool daily_ok = open_meteo_fetch_daily(client, response, latitude, longitude, daily_out, daily_count, current_out);
            return current_ok && hourly_ok && daily_ok;
        }
    }
}
