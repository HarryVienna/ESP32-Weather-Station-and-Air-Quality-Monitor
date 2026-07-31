#include <stdio.h>
#include <time.h>

#include "esp_log.h"

#include "open_meteo_provider.h"

static const char *TAG = "open_meteo_provider";

static const char *WEATHER_URL_CURRENT =
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%s&longitude=%s&"
        "current=temperature_2m,dew_point_2m,relative_humidity_2m,pressure_msl,apparent_temperature,"
        "is_day,weather_code,cloud_cover,wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index&"
        "timeformat=unixtime&timezone=auto";

static const char *WEATHER_URL_HOURLY =
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%s&longitude=%s&"
        "hourly=temperature_2m,dew_point_2m,precipitation_probability,rain,showers,snowfall,"
        "wind_speed_10m,wind_gusts_10m,sunshine_duration,cloud_cover,is_day&"
        "timeformat=unixtime&timezone=auto&forecast_days=3";

static const char *WEATHER_URL_DAILY =
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%s&longitude=%s&"
        "daily=temperature_2m_max,temperature_2m_min,"
        "daylight_duration,sunshine_duration,"
        "rain_sum,showers_sum,snowfall_sum,precipitation_probability_max,"
        "wind_speed_10m_max,wind_gusts_10m_max,sunrise,sunset&"
        "timeformat=unixtime&timezone=auto&forecast_days=%d";

bool open_meteo_fetch_current(esp_http_client_handle_t client, weather_http_response_t *response,
                               const char *latitude, const char *longitude, current_weather_data_t *out) {
    char url[512];
    snprintf(url, sizeof(url), WEATHER_URL_CURRENT, latitude, longitude);
    ESP_LOGI(TAG, "Call current weather API: %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    cJSON *current = cJSON_GetObjectItem(json, "current");

    if (current == NULL || !cJSON_IsObject(current)) {
        ESP_LOGE(TAG, "Missing or invalid 'current' object in API response");
    } else {
        cJSON *temp_2m = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *dew_point = cJSON_GetObjectItem(current, "dew_point_2m");
        cJSON *humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");
        cJSON *pressure_msl = cJSON_GetObjectItem(current, "pressure_msl");
        cJSON *apparent_temp = cJSON_GetObjectItem(current, "apparent_temperature");
        cJSON *is_day_item = cJSON_GetObjectItem(current, "is_day");
        cJSON *weather_code_item = cJSON_GetObjectItem(current, "weather_code");
        cJSON *cloud_cover_item = cJSON_GetObjectItem(current, "cloud_cover");
        cJSON *wind_speed = cJSON_GetObjectItem(current, "wind_speed_10m");
        cJSON *wind_dir = cJSON_GetObjectItem(current, "wind_direction_10m");
        cJSON *wind_gusts = cJSON_GetObjectItem(current, "wind_gusts_10m");
        cJSON *uv = cJSON_GetObjectItem(current, "uv_index");

        if (!temp_2m || !cJSON_IsNumber(temp_2m) ||
            !dew_point || !cJSON_IsNumber(dew_point) ||
            !humidity || !cJSON_IsNumber(humidity) ||
            !pressure_msl || !cJSON_IsNumber(pressure_msl) ||
            !apparent_temp || !cJSON_IsNumber(apparent_temp) ||
            !is_day_item || !cJSON_IsNumber(is_day_item) ||
            !weather_code_item || !cJSON_IsNumber(weather_code_item) ||
            !cloud_cover_item || !cJSON_IsNumber(cloud_cover_item) ||
            !wind_speed || !cJSON_IsNumber(wind_speed) ||
            !wind_dir || !cJSON_IsNumber(wind_dir) ||
            !uv || !cJSON_IsNumber(uv)) {

            ESP_LOGE(TAG, "Current weather data incomplete - missing required fields");
        } else {
            out->temperature_2m = temp_2m->valuedouble;
            out->dew_point_2m = dew_point->valuedouble;
            out->relative_humidity_2m = humidity->valueint;
            out->pressure_msl = pressure_msl->valuedouble;
            out->apparent_temperature = apparent_temp->valuedouble;
            out->is_day = is_day_item->valueint;
            out->weather_code = weather_code_item->valueint;
            out->cloud_cover = cloud_cover_item->valueint;
            out->wind_speed_10m = wind_speed->valuedouble;
            out->wind_direction_10m = wind_dir->valueint;
            // Gusts are sometimes missing from the API during calm winds -
            // in that case use wind speed as the gust value instead of
            // leaving it empty
            out->wind_gusts_10m = (wind_gusts && cJSON_IsNumber(wind_gusts)) ? wind_gusts->valuedouble : wind_speed->valuedouble;
            out->uv_index = uv->valuedouble;

            success = true;
            ESP_LOGI(TAG, "Current weather data successfully retrieved");
        }
    }

    cJSON_Delete(json);
    return success;
}

bool open_meteo_fetch_hourly(esp_http_client_handle_t client, weather_http_response_t *response,
                              const char *latitude, const char *longitude, hourly_weather_data_t *out, int count) {
    char url[512];
    snprintf(url, sizeof(url), WEATHER_URL_HOURLY, latitude, longitude);
    ESP_LOGI(TAG, "Call hourly weather API: %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    int currentHour = timeinfo.tm_hour;

    cJSON *hourly = cJSON_GetObjectItem(json, "hourly");

    if (hourly == NULL || !cJSON_IsObject(hourly)) {
        ESP_LOGE(TAG, "Missing or invalid 'hourly' object in API response");
    } else {
        cJSON *time_arr = cJSON_GetObjectItem(hourly, "time");
        cJSON *temperature_2m = cJSON_GetObjectItem(hourly, "temperature_2m");
        cJSON *dew_point_2m = cJSON_GetObjectItem(hourly, "dew_point_2m");
        cJSON *precipitation_probability = cJSON_GetObjectItem(hourly, "precipitation_probability");
        cJSON *rain = cJSON_GetObjectItem(hourly, "rain");
        cJSON *showers = cJSON_GetObjectItem(hourly, "showers");
        cJSON *snowfall = cJSON_GetObjectItem(hourly, "snowfall");
        cJSON *wind_speed_10m = cJSON_GetObjectItem(hourly, "wind_speed_10m");
        cJSON *wind_gusts_10m = cJSON_GetObjectItem(hourly, "wind_gusts_10m");
        cJSON *sunshine_duration = cJSON_GetObjectItem(hourly, "sunshine_duration");
        cJSON *cloud_cover = cJSON_GetObjectItem(hourly, "cloud_cover");
        cJSON *is_day = cJSON_GetObjectItem(hourly, "is_day");

        if (!time_arr || !cJSON_IsArray(time_arr) ||
            !temperature_2m || !cJSON_IsArray(temperature_2m) ||
            !dew_point_2m || !cJSON_IsArray(dew_point_2m) ||
            !precipitation_probability || !cJSON_IsArray(precipitation_probability) ||
            !rain || !cJSON_IsArray(rain) ||
            !showers || !cJSON_IsArray(showers) ||
            !snowfall || !cJSON_IsArray(snowfall) ||
            !wind_speed_10m || !cJSON_IsArray(wind_speed_10m) ||
            !sunshine_duration || !cJSON_IsArray(sunshine_duration) ||
            !cloud_cover || !cJSON_IsArray(cloud_cover) ||
            !is_day || !cJSON_IsArray(is_day)) {

            ESP_LOGE(TAG, "Hourly weather data incomplete - missing required arrays");
        } else {
            int time_array_size = cJSON_GetArraySize(time_arr);
            int required_size = currentHour + count;

            if (time_array_size < required_size) {
                ESP_LOGE(TAG, "Insufficient hourly data: need %d items, have %d (current hour: %d)",
                         required_size, time_array_size, currentHour);
            } else {
                bool hourly_data_valid = true;
                for (int i = 0; i < count; i++) {
                    int array_index = i + currentHour;

                    cJSON *time_item = cJSON_GetArrayItem(time_arr, array_index);
                    cJSON *temp_item = cJSON_GetArrayItem(temperature_2m, array_index);
                    cJSON *dew_item = cJSON_GetArrayItem(dew_point_2m, array_index);
                    cJSON *precip_item = cJSON_GetArrayItem(precipitation_probability, array_index);
                    cJSON *rain_item = cJSON_GetArrayItem(rain, array_index);
                    cJSON *shower_item = cJSON_GetArrayItem(showers, array_index);
                    cJSON *snow_item = cJSON_GetArrayItem(snowfall, array_index);
                    cJSON *wind_speed_item = cJSON_GetArrayItem(wind_speed_10m, array_index);
                    cJSON *wind_gust_item = cJSON_GetArrayItem(wind_gusts_10m, array_index);
                    cJSON *sun_item = cJSON_GetArrayItem(sunshine_duration, array_index);
                    cJSON *cloud_item = cJSON_GetArrayItem(cloud_cover, array_index);
                    cJSON *day_item = cJSON_GetArrayItem(is_day, array_index);

                    if (!time_item || !cJSON_IsNumber(time_item) ||
                        !temp_item || !cJSON_IsNumber(temp_item) ||
                        !dew_item || !cJSON_IsNumber(dew_item) ||
                        !precip_item || !cJSON_IsNumber(precip_item) ||
                        !rain_item || !cJSON_IsNumber(rain_item) ||
                        !shower_item || !cJSON_IsNumber(shower_item) ||
                        !snow_item || !cJSON_IsNumber(snow_item) ||
                        !wind_speed_item || !cJSON_IsNumber(wind_speed_item) ||
                        !sun_item || !cJSON_IsNumber(sun_item) ||
                        !cloud_item || !cJSON_IsNumber(cloud_item) ||
                        !day_item || !cJSON_IsNumber(day_item)) {

                        ESP_LOGE(TAG, "Hourly data item at index %d is invalid", array_index);
                        hourly_data_valid = false;
                        break;
                    }

                    time_t unixTimestamp = (time_t)time_item->valueint;
                    localtime_r(&unixTimestamp, &out[i].time);

                    out[i].temperature_2m = temp_item->valuedouble;
                    out[i].dew_point_2m = dew_item->valuedouble;
                    out[i].precipitation_probability = precip_item->valuedouble;
                    out[i].rain = rain_item->valuedouble;
                    out[i].showers = shower_item->valuedouble;
                    out[i].snowfall = snow_item->valuedouble;
                    out[i].wind_speed_10m = wind_speed_item->valuedouble;
                    // Gusts are sometimes missing from the API during calm
                    // winds - in that case use wind speed as the gust
                    // value instead of leaving it empty
                    out[i].wind_gusts_10m = (wind_gust_item && cJSON_IsNumber(wind_gust_item)) ? wind_gust_item->valuedouble : wind_speed_item->valuedouble;
                    out[i].sunshine_duration = sun_item->valuedouble;
                    out[i].cloud_cover = cloud_item->valuedouble;
                    out[i].is_day = day_item->valueint ? true : false;
                }

                if (hourly_data_valid) {
                    success = true;
                    ESP_LOGI(TAG, "Hourly weather data successfully retrieved");
                } else {
                    ESP_LOGE(TAG, "Hourly data validation failed");
                }
            }
        }
    }

    cJSON_Delete(json);
    return success;
}

bool open_meteo_fetch_daily(esp_http_client_handle_t client, weather_http_response_t *response,
                             const char *latitude, const char *longitude, daily_weather_data_t *out, int count) {
    char url[512];
    snprintf(url, sizeof(url), WEATHER_URL_DAILY, latitude, longitude, count);
    ESP_LOGI(TAG, "Call daily weather API: %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    cJSON *daily = cJSON_GetObjectItem(json, "daily");

    if (daily == NULL || !cJSON_IsObject(daily)) {
        ESP_LOGE(TAG, "Missing or invalid 'daily' object in API response");
    } else {
        cJSON *time_array = cJSON_GetObjectItem(daily, "time");
        cJSON *temperature_2m_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *temperature_2m_min = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *daylight_duration = cJSON_GetObjectItem(daily, "daylight_duration");
        cJSON *sunshine_duration = cJSON_GetObjectItem(daily, "sunshine_duration");
        cJSON *rain_sum = cJSON_GetObjectItem(daily, "rain_sum");
        cJSON *showers_sum = cJSON_GetObjectItem(daily, "showers_sum");
        cJSON *snowfall_sum = cJSON_GetObjectItem(daily, "snowfall_sum");
        cJSON *precipitation_probability_max = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        cJSON *wind_speed_10m_max = cJSON_GetObjectItem(daily, "wind_speed_10m_max");
        cJSON *wind_gusts_10m_max = cJSON_GetObjectItem(daily, "wind_gusts_10m_max");
        cJSON *sunrise = cJSON_GetObjectItem(daily, "sunrise");
        cJSON *sunset = cJSON_GetObjectItem(daily, "sunset");

        if (!time_array || !cJSON_IsArray(time_array) ||
            !temperature_2m_max || !cJSON_IsArray(temperature_2m_max) ||
            !temperature_2m_min || !cJSON_IsArray(temperature_2m_min) ||
            !daylight_duration || !cJSON_IsArray(daylight_duration) ||
            !sunshine_duration || !cJSON_IsArray(sunshine_duration) ||
            !rain_sum || !cJSON_IsArray(rain_sum) ||
            !showers_sum || !cJSON_IsArray(showers_sum) ||
            !snowfall_sum || !cJSON_IsArray(snowfall_sum) ||
            !precipitation_probability_max || !cJSON_IsArray(precipitation_probability_max) ||
            !wind_speed_10m_max || !cJSON_IsArray(wind_speed_10m_max) ||
            !sunrise || !cJSON_IsArray(sunrise) ||
            !sunset || !cJSON_IsArray(sunset)) {

            ESP_LOGE(TAG, "Daily weather data incomplete - missing required arrays");
        } else {
            int time_array_size = cJSON_GetArraySize(time_array);

            if (time_array_size < count) {
                ESP_LOGE(TAG, "Insufficient daily data: need %d days, have %d", count, time_array_size);
            } else {
                bool daily_data_valid = true;
                for (int i = 0; i < count; i++) {
                    cJSON *time_item = cJSON_GetArrayItem(time_array, i);
                    cJSON *temp_max_item = cJSON_GetArrayItem(temperature_2m_max, i);
                    cJSON *temp_min_item = cJSON_GetArrayItem(temperature_2m_min, i);
                    cJSON *daylight_item = cJSON_GetArrayItem(daylight_duration, i);
                    cJSON *sunshine_item = cJSON_GetArrayItem(sunshine_duration, i);
                    cJSON *rain_item = cJSON_GetArrayItem(rain_sum, i);
                    cJSON *shower_item = cJSON_GetArrayItem(showers_sum, i);
                    cJSON *snow_item = cJSON_GetArrayItem(snowfall_sum, i);
                    cJSON *precip_item = cJSON_GetArrayItem(precipitation_probability_max, i);
                    cJSON *wind_speed_item = cJSON_GetArrayItem(wind_speed_10m_max, i);
                    cJSON *wind_gust_item = cJSON_GetArrayItem(wind_gusts_10m_max, i);
                    cJSON *sunrise_item = cJSON_GetArrayItem(sunrise, i);
                    cJSON *sunset_item = cJSON_GetArrayItem(sunset, i);

                    if (!time_item || !cJSON_IsNumber(time_item) ||
                        !temp_max_item || !cJSON_IsNumber(temp_max_item) ||
                        !temp_min_item || !cJSON_IsNumber(temp_min_item) ||
                        !daylight_item || !cJSON_IsNumber(daylight_item) ||
                        !sunshine_item || !cJSON_IsNumber(sunshine_item) ||
                        !rain_item || !cJSON_IsNumber(rain_item) ||
                        !shower_item || !cJSON_IsNumber(shower_item) ||
                        !snow_item || !cJSON_IsNumber(snow_item) ||
                        !precip_item || !cJSON_IsNumber(precip_item) ||
                        !wind_speed_item || !cJSON_IsNumber(wind_speed_item) ||
                        !sunrise_item || !cJSON_IsNumber(sunrise_item) ||
                        !sunset_item || !cJSON_IsNumber(sunset_item)) {

                        ESP_LOGE(TAG, "Daily data item at index %d is invalid", i);
                        daily_data_valid = false;
                        break;
                    }

                    time_t timeTimestamp = (time_t)time_item->valueint;
                    localtime_r(&timeTimestamp, &out[i].time);

                    out[i].temperature_2m_max = temp_max_item->valuedouble;
                    out[i].temperature_2m_min = temp_min_item->valuedouble;
                    out[i].daylight_duration = daylight_item->valuedouble;
                    out[i].sunshine_duration = sunshine_item->valuedouble;
                    out[i].rain_sum = rain_item->valuedouble;
                    out[i].showers_sum = shower_item->valuedouble;
                    out[i].snowfall_sum = snow_item->valuedouble;
                    out[i].precipitation_probability_max = precip_item->valuedouble;
                    out[i].wind_speed_10m_max = wind_speed_item->valuedouble;
                    // Gusts are sometimes missing from the API during calm
                    // winds - in that case use wind speed as the gust
                    // value instead of leaving it empty
                    out[i].wind_gusts_10m_max = (wind_gust_item && cJSON_IsNumber(wind_gust_item)) ? wind_gust_item->valuedouble : wind_speed_item->valuedouble;

                    time_t sunriseTimestamp = (time_t)sunrise_item->valueint;
                    localtime_r(&sunriseTimestamp, &out[i].sunrise);

                    time_t sunsetTimestamp = (time_t)sunset_item->valueint;
                    localtime_r(&sunsetTimestamp, &out[i].sunset);
                }

                if (daily_data_valid) {
                    success = true;
                    ESP_LOGI(TAG, "Daily weather data successfully retrieved");
                } else {
                    ESP_LOGE(TAG, "Daily data validation failed");
                }
            }
        }
    }

    cJSON_Delete(json);
    return success;
}
