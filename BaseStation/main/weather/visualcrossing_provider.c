#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "visualcrossing_provider.h"

static const char *TAG = "visualcrossing_provider";

/* Timeline Weather API (https://www.visualcrossing.com/resources/documentation/weather-api/timeline-weather-api/).
 * Unlike Open-Meteo/OWM, a single call with include=days,hours,current
 * delivers everything at once - hence just one URL and one parse pass
 * here instead of three separate requests. Per the docs, a forecast call
 * also costs a flat 1 record regardless of how many days/hours are
 * requested ("A full 15-day forecast ... counts as a single record. This
 * is true even for an hourly forecast.") - unlike historical data, which
 * is billed per row. */
/* No date range needed in the path - without one, VC returns the default
 * 15-day forecast, of which only the first `daily_count`/`hourly_count`
 * entries are used via NUM_DAYS/NUM_HOURS (lv_daily_chart.h/
 * lv_hourly_chart.h), the rest is ignored. */
static const char *VC_URL_TIMELINE =
        "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/%s,%s?"
        "unitGroup=metric&key=%s&contentType=json&include=days,hours,current&iconSet=icons2";

static bool req_num(cJSON *obj, const char *key, double *out) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static double opt_num(cJSON *obj, const char *key, double def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : def;
}

/* is_day isn't in any field directly. Primarily, the timestamp is checked
 * against sunriseEpoch/sunsetEpoch (exact); if those are missing, the
 * icon's day/night suffix is used (e.g. "clear-day"/"clear-night" - not
 * all icons like "rain"/"cloudy" have a suffix); day is the last-resort
 * fallback. */
static bool vc_is_day(double dt, double sunrise, double sunset, cJSON *item) {
    if (sunrise > 0 && sunset > 0) {
        return dt >= sunrise && dt < sunset;
    }

    cJSON *icon = cJSON_GetObjectItem(item, "icon");
    if (icon && cJSON_IsString(icon)) {
        size_t len = strlen(icon->valuestring);
        if (len >= 4 && strcmp(icon->valuestring + len - 4, "-day") == 0) {
            return true;
        }
        if (len >= 6 && strcmp(icon->valuestring + len - 6, "-night") == 0) {
            return false;
        }
    }

    return true;
}

/* Rough mapping of the VC icon set "icons2" (see iconSet=icons2 above,
 * docs: defining-icon-set-in-the-weather-api) onto the 28 existing WMO
 * icons (see icon_mapping_day/night in gui_weather.c). icons2 has exactly
 * 16 values, no distinct "thunder"/"hail" like the standard icon set.
 * Lossy, but confirmed sufficient, similar to the OWM integration. */
static int vc_icon_to_wmo(const char *icon) {
    static const struct { const char *icon; int wmo; } map[] = {
        {"clear-day", 0}, {"clear-night", 0},
        {"partly-cloudy-day", 2}, {"partly-cloudy-night", 2},
        {"cloudy", 3}, {"wind", 3},
        {"fog", 45},
        {"rain", 61},
        {"showers-day", 80}, {"showers-night", 80},
        {"snow", 71},
        {"snow-showers-day", 85}, {"snow-showers-night", 85},
        {"thunder-rain", 95},
        {"thunder-showers-day", 96}, {"thunder-showers-night", 96},
    };

    if (icon != NULL) {
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
            if (strcmp(icon, map[i].icon) == 0) {
                return map[i].wmo;
            }
        }
    }

    return 3; // fallback: overcast
}

static bool parse_current(cJSON *current, current_weather_data_t *out) {
    double temp, pressure, humidity, cloudcover, windspeed, winddir, dt;

    if (!req_num(current, "temp", &temp) ||
        !req_num(current, "pressure", &pressure) ||
        !req_num(current, "humidity", &humidity) ||
        !req_num(current, "cloudcover", &cloudcover) ||
        !req_num(current, "windspeed", &windspeed) ||
        !req_num(current, "winddir", &winddir) ||
        !req_num(current, "datetimeEpoch", &dt)) {
        ESP_LOGE(TAG, "Current weather data incomplete - missing required fields");
        return false;
    }

    double sunrise = opt_num(current, "sunriseEpoch", 0.0);
    double sunset = opt_num(current, "sunsetEpoch", 0.0);
    cJSON *icon = cJSON_GetObjectItem(current, "icon");

    out->temperature_2m = temp;
    out->dew_point_2m = opt_num(current, "dew", temp);
    out->relative_humidity_2m = (int)humidity;
    out->pressure_msl = pressure;
    out->apparent_temperature = opt_num(current, "feelslike", temp);
    out->is_day = vc_is_day(dt, sunrise, sunset, current) ? 1 : 0;
    out->weather_code = vc_icon_to_wmo(cJSON_IsString(icon) ? icon->valuestring : NULL);
    out->cloud_cover = (int)cloudcover;
    out->wind_speed_10m = windspeed; // unitGroup=metric already delivers km/h
    out->wind_direction_10m = (int)winddir;
    // Gusts are sometimes missing from VC (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m = opt_num(current, "windgust", windspeed);
    out->uv_index = opt_num(current, "uvindex", 0.0);

    return true;
}

static bool parse_hourly_item(cJSON *item, double day_sunrise, double day_sunset, hourly_weather_data_t *out) {
    double dt, temp, cloudcover, windspeed;

    if (!req_num(item, "datetimeEpoch", &dt) ||
        !req_num(item, "temp", &temp) ||
        !req_num(item, "cloudcover", &cloudcover) ||
        !req_num(item, "windspeed", &windspeed)) {
        ESP_LOGE(TAG, "Hourly data item invalid - missing required fields");
        return false;
    }

    bool is_day = vc_is_day(dt, day_sunrise, day_sunset, item);
    time_t ts = (time_t)dt;
    localtime_r(&ts, &out->time);

    out->temperature_2m = temp;
    out->dew_point_2m = opt_num(item, "dew", temp);
    out->precipitation_probability = opt_num(item, "precipprob", 0.0); // already 0-100
    out->rain = opt_num(item, "precip", 0.0);
    out->showers = 0.0; // VC doesn't distinguish showers from regular rain
    out->snowfall = opt_num(item, "snow", 0.0); // unitGroup=metric already delivers cm
    out->wind_speed_10m = windspeed;
    // Gusts are sometimes missing from VC (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m = opt_num(item, "windgust", windspeed);
    out->cloud_cover = cloudcover;
    out->is_day = is_day;
    // VC doesn't provide sunshine duration - approximated via cloud cover
    out->sunshine_duration = is_day ? (100.0 - cloudcover) / 100.0 * 3600.0 : 0.0;

    return true;
}

static bool parse_daily_item(cJSON *item, daily_weather_data_t *out) {
    double dt, temp_max, temp_min, cloudcover, windspeed, sunrise_ts, sunset_ts;

    if (!req_num(item, "datetimeEpoch", &dt) ||
        !req_num(item, "tempmax", &temp_max) ||
        !req_num(item, "tempmin", &temp_min) ||
        !req_num(item, "cloudcover", &cloudcover) ||
        !req_num(item, "windspeed", &windspeed) ||
        !req_num(item, "sunriseEpoch", &sunrise_ts) ||
        !req_num(item, "sunsetEpoch", &sunset_ts)) {
        ESP_LOGE(TAG, "Daily data item invalid - missing required fields");
        return false;
    }

    double daylight_duration = sunset_ts - sunrise_ts;

    time_t dt_ts = (time_t)dt;
    localtime_r(&dt_ts, &out->time);
    time_t sunrise_time = (time_t)sunrise_ts;
    localtime_r(&sunrise_time, &out->sunrise);
    time_t sunset_time = (time_t)sunset_ts;
    localtime_r(&sunset_time, &out->sunset);

    out->temperature_2m_max = temp_max;
    out->temperature_2m_min = temp_min;
    out->daylight_duration = daylight_duration;
    // VC doesn't provide sunshine duration - approximated via cloud cover
    out->sunshine_duration = (100.0 - cloudcover) / 100.0 * daylight_duration;
    out->rain_sum = opt_num(item, "precip", 0.0);
    out->showers_sum = 0.0;
    out->snowfall_sum = opt_num(item, "snow", 0.0); // unitGroup=metric already delivers cm
    out->precipitation_probability_max = opt_num(item, "precipprob", 0.0); // already 0-100
    out->wind_speed_10m_max = windspeed; // already the daily max per the docs
    // Gusts are sometimes missing from VC (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m_max = opt_num(item, "windgust", windspeed);

    return true;
}

bool visualcrossing_fetch_all(esp_http_client_handle_t client, weather_http_response_t *response,
                               const char *latitude, const char *longitude, const char *api_key,
                               current_weather_data_t *current_out, hourly_weather_data_t *hourly_out,
                               int hourly_count, daily_weather_data_t *daily_out, int daily_count) {
    struct tm timeinfo;
    time_t now = time(NULL);
    localtime_r(&now, &timeinfo);
    int currentHour = timeinfo.tm_hour;

    char url[512];
    snprintf(url, sizeof(url), VC_URL_TIMELINE, latitude, longitude, api_key);
    ESP_LOGI(TAG, "Call weather API (VisualCrossing): %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    cJSON *current = cJSON_GetObjectItem(json, "currentConditions");
    bool current_ok = current != NULL && cJSON_IsObject(current) && parse_current(current, current_out);
    if (!current_ok) {
        ESP_LOGE(TAG, "Missing/invalid 'currentConditions' object in API response");
    }

    cJSON *days = cJSON_GetObjectItem(json, "days");
    bool days_present = days != NULL && cJSON_IsArray(days);
    if (!days_present) {
        ESP_LOGE(TAG, "Missing or invalid 'days' array in API response");
    }

    bool daily_ok = false;
    if (days_present) {
        if (cJSON_GetArraySize(days) < daily_count) {
            ESP_LOGE(TAG, "Insufficient daily data: need %d days, have %d", daily_count, cJSON_GetArraySize(days));
        } else {
            daily_ok = true;
            for (int i = 0; i < daily_count && daily_ok; i++) {
                daily_ok = parse_daily_item(cJSON_GetArrayItem(days, i), &daily_out[i]);
            }
        }
    }

    bool hourly_ok = false;
    if (days_present) {
        int skip = currentHour;
        int collected = 0;
        bool valid = true;
        int day_count = cJSON_GetArraySize(days);

        for (int d = 0; d < day_count && collected < hourly_count && valid; d++) {
            cJSON *day = cJSON_GetArrayItem(days, d);
            double day_sunrise = opt_num(day, "sunriseEpoch", 0.0);
            double day_sunset = opt_num(day, "sunsetEpoch", 0.0);
            cJSON *hours = cJSON_GetObjectItem(day, "hours");

            if (hours == NULL || !cJSON_IsArray(hours)) {
                continue;
            }

            int hour_count = cJSON_GetArraySize(hours);
            for (int h = 0; h < hour_count && collected < hourly_count; h++) {
                if (skip > 0) {
                    skip--;
                    continue;
                }

                if (!parse_hourly_item(cJSON_GetArrayItem(hours, h), day_sunrise, day_sunset, &hourly_out[collected])) {
                    valid = false;
                    break;
                }
                collected++;
            }
        }

        hourly_ok = valid && collected == hourly_count;
        if (!hourly_ok) {
            ESP_LOGE(TAG, "Insufficient/invalid hourly data: need %d items, got %d", hourly_count, collected);
        }
    }

    bool success = current_ok && hourly_ok && daily_ok;
    if (success) {
        ESP_LOGI(TAG, "Weather data successfully retrieved (VisualCrossing, single call)");
    }

    cJSON_Delete(json);
    return success;
}
