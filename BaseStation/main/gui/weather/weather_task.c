#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs/preferences.h"

#include "gui_weather.h"

#include "weather_task.h"
#include "weather/weather_data.h"
#include "http/http_client.h"
#include "weather/weather_provider.h"
#include "lvgl/lv_hourly_chart.h"
#include "lvgl/lv_daily_chart.h"

static const char* TAG = "weather_task";

/**
 * @brief Validate latitude/longitude coordinate strings
 *
 * @param lat Latitude string (expected range: -90 to 90)
 * @param lon Longitude string (expected range: -180 to 180)
 * @return true if valid, false otherwise
 */
static bool validate_coordinates(const char* lat, const char* lon) {
    if (lat == NULL || lon == NULL || lat[0] == '\0' || lon[0] == '\0') {
        ESP_LOGE(TAG, "Coordinates are NULL or empty");
        return false;
    }

    char* endptr_lat;
    char* endptr_lon;
    double lat_val = strtod(lat, &endptr_lat);
    double lon_val = strtod(lon, &endptr_lon);

    if (*endptr_lat != '\0' || *endptr_lon != '\0') {
        ESP_LOGE(TAG, "Invalid coordinate format: lat='%s', lon='%s'", lat, lon);
        return false;
    }

    if (lat_val < -90.0 || lat_val > 90.0) {
        ESP_LOGE(TAG, "Latitude out of range: %f (must be -90 to 90)", lat_val);
        return false;
    }

    if (lon_val < -180.0 || lon_val > 180.0) {
        ESP_LOGE(TAG, "Longitude out of range: %f (must be -180 to 180)", lon_val);
        return false;
    }

    return true;
}

/**
 * @brief Seconds until the next :00/:15/:30/:45 wall-clock mark
 *
 * Aligning fetches to quarter-hour marks (instead of just sleeping 15
 * minutes after each fetch, which drifts with whatever second the task
 * happened to start on) keeps the worst-case staleness after an hour/day
 * boundary down to one fetch cycle instead of up to ~15 extra minutes.
 * Falls back to a flat 15 minutes if the system clock isn't synchronized
 * yet, since the computed delay would otherwise be meaningless.
 */
static int delay_to_next_quarter_hour(void) {
    time_t now;
    time(&now);

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (tm_now.tm_year + 1900 < 2020) {
        return 60 * 15;
    }

    int seconds_into_quarter = (tm_now.tm_min % 15) * 60 + tm_now.tm_sec;
    int delay_sec = (60 * 15) - seconds_into_quarter;
    if (delay_sec <= 0 || delay_sec > 60 * 15) {
        delay_sec = 60 * 15;
    }
    return delay_sec;
}

/**
 * @brief     Task for retrieving and displaying weather data from the configured API
 *
 * @param     pvParameter   Pointer to task parameters (not used in this function)
 *
 * @details   Reads the provider stored in NVS ("weather_api": 0=Open-Meteo,
 *            1=OpenWeatherMap, 2=VisualCrossing) and fetches current/hourly/daily
 *            via weather_provider_fetch_all(). The actual HTTP/JSON handling
 *            lives provider-specifically in weather/open_meteo_provider.c,
 *            weather/openweathermap_provider.c, and weather/visualcrossing_provider.c
 *            respectively.
 */
void weather_task(void *pvParameter) {

    ESP_LOGI(TAG, "Start Weather task");

    nvs_handle_t nvs_handle;
    nvs_open("weatherstation", NVS_READONLY, &nvs_handle);

    char* latitude = get_string_from_nvs(nvs_handle, "latitude", "");
    char* longitude = get_string_from_nvs(nvs_handle, "longitude", "");
    weather_provider_t provider = (weather_provider_t)get_uint8_from_nvs(nvs_handle, "weather_api", WEATHER_PROVIDER_OPEN_METEO);

    // Each provider with its own API key has its own NVS key (see
    // apply_appid_for_provider() in gui_setup_screen_actions.c) - Open-Meteo doesn't need one.
    char* api_key;
    switch (provider) {
        case WEATHER_PROVIDER_OPENWEATHERMAP:
            api_key = get_string_from_nvs(nvs_handle, "appid_owm", "");
            break;
        case WEATHER_PROVIDER_VISUALCROSSING:
            api_key = get_string_from_nvs(nvs_handle, "appid_vc", "");
            break;
        case WEATHER_PROVIDER_OPEN_METEO:
        default:
            api_key = strdup("");
            break;
    }

    nvs_close(nvs_handle);

    if (!validate_coordinates(latitude, longitude)) {
        ESP_LOGE(TAG, "Invalid coordinates from NVS. Task cannot proceed.");
        free(latitude);
        free(longitude);
        free(api_key);
        vTaskDelete(NULL);
        return;
    }

    if ((provider == WEATHER_PROVIDER_OPENWEATHERMAP || provider == WEATHER_PROVIDER_VISUALCROSSING) && api_key[0] == '\0') {
        ESP_LOGE(TAG, "OpenWeatherMap/VisualCrossing ausgewaehlt, aber kein API-Key in NVS hinterlegt. Task cannot proceed.");
        free(latitude);
        free(longitude);
        free(api_key);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Using coordinates: lat=%s, lon=%s, provider=%d", latitude, longitude, provider);

    current_weather_data_t current_data = {0};
    hourly_weather_data_t hourly_data[NUM_HOURS] = {0};
    daily_weather_data_t daily_data[NUM_DAYS] = {0};

    http_response_t response = {0};
    esp_http_client_handle_t client = http_client_create(&response);

    for (;;) {
        bool success = weather_provider_fetch_all(provider, client, &response, latitude, longitude, api_key,
                                                    &current_data, hourly_data, NUM_HOURS, daily_data, NUM_DAYS);

        if (success) {
            ESP_LOGI(TAG, "All weather data retrieved successfully - updating display");
            disp_weather(&current_data, hourly_data, daily_data);
        } else {
            ESP_LOGW(TAG, "Weather data incomplete - skipping display update");
        }

        vTaskDelay(pdMS_TO_TICKS(1000 * delay_to_next_quarter_hour()));
    }
}
