#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "openweathermap_provider.h"

static const char *TAG = "openweathermap_provider";

/* One Call API 4.0 (https://openweathermap.org/api/one-call-4) - anders als
 * 3.0 sind current/hourly/daily eigene Endpunkte, kein kombinierter Call.
 * Antworten sind {"data": [...], "next": "...", "prev": "..."} - hourly
 * liefert max. 20 Eintraege pro Antwort, daher Pagination ueber "next". */
static const char *OWM_URL_CURRENT =
        "https://api.openweathermap.org/data/4.0/onecall/current?"
        "lat=%s&lon=%s&appid=%s&units=metric";

static const char *OWM_URL_HOURLY =
        "https://api.openweathermap.org/data/4.0/onecall/timeline/1h?"
        "lat=%s&lon=%s&appid=%s&units=metric&cnt=20";

static const char *OWM_URL_DAILY =
        "https://api.openweathermap.org/data/4.0/onecall/timeline/1day?"
        "lat=%s&lon=%s&appid=%s&units=metric&cnt=%d";

static bool req_num(cJSON *obj, const char *key, double *out) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static bool req_nested_num(cJSON *obj, const char *outer, const char *inner, double *out) {
    cJSON *o = cJSON_GetObjectItem(obj, outer);
    if (!o || !cJSON_IsObject(o)) {
        return false;
    }
    return req_num(o, inner, out);
}

static double opt_num(cJSON *obj, const char *key, double def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : def;
}

static double opt_nested_num(cJSON *obj, const char *outer, const char *inner, double def) {
    cJSON *o = cJSON_GetObjectItem(obj, outer);
    if (!o || !cJSON_IsObject(o)) {
        return def;
    }
    return opt_num(o, inner, def);
}

/* is_day steht in keinem Feld direkt - OWM haengt an den Icon-Code ein 'd'
 * oder 'n' an (z.B. "01d"/"01n"). Fehlt das Icon, wird Tag angenommen. */
static bool owm_is_day(cJSON *item) {
    cJSON *weather = cJSON_GetObjectItem(item, "weather");
    if (weather && cJSON_IsArray(weather) && cJSON_GetArraySize(weather) > 0) {
        cJSON *w0 = cJSON_GetArrayItem(weather, 0);
        cJSON *icon = cJSON_GetObjectItem(w0, "icon");
        if (icon && cJSON_IsString(icon) && icon->valuestring[0] != '\0') {
            size_t len = strlen(icon->valuestring);
            return icon->valuestring[len - 1] != 'n';
        }
    }
    return true;
}

static bool owm_weather_id(cJSON *item, int *out_id) {
    cJSON *weather = cJSON_GetObjectItem(item, "weather");
    if (!weather || !cJSON_IsArray(weather) || cJSON_GetArraySize(weather) < 1) {
        return false;
    }
    cJSON *w0 = cJSON_GetArrayItem(weather, 0);
    cJSON *id = cJSON_GetObjectItem(w0, "id");
    if (!id || !cJSON_IsNumber(id)) {
        return false;
    }
    *out_id = id->valueint;
    return true;
}

/* Grobe Abbildung der OWM-Conditioncodes auf die 28 vorhandenen WMO-Icons
 * (siehe icon_mapping_day/night in gui_weather.c). */
static int owm_weather_id_to_wmo(int owm_id) {
    if (owm_id == 800) return 0;   // klarer Himmel
    if (owm_id == 801) return 1;   // wenige Wolken
    if (owm_id == 802) return 2;   // vereinzelte Wolken
    if (owm_id == 803 || owm_id == 804) return 3; // bewoelkt/bedeckt

    if (owm_id >= 701 && owm_id <= 771) return 45; // Nebel/Dunst/Sand/Rauch

    if (owm_id == 300 || owm_id == 310) return 51; // leichter Sprühregen
    if (owm_id == 301 || owm_id == 311 || owm_id == 321) return 53;
    if (owm_id == 302 || owm_id == 312 || owm_id == 313 || owm_id == 314) return 55;

    if (owm_id == 500) return 61;  // leichter Regen
    if (owm_id == 501) return 63;  // maessiger Regen
    if (owm_id == 502 || owm_id == 503 || owm_id == 504) return 65; // starker Regen
    if (owm_id == 511) return 66;  // gefrierender Regen

    if (owm_id == 520) return 80;  // leichter Regenschauer
    if (owm_id == 521) return 81;
    if (owm_id == 522 || owm_id == 531) return 82;

    if (owm_id == 600 || owm_id == 612 || owm_id == 615 || owm_id == 620) return 71; // leichter Schnee
    if (owm_id == 601) return 73;
    if (owm_id == 602 || owm_id == 622) return 75;
    if (owm_id == 611 || owm_id == 613 || owm_id == 616 || owm_id == 621) return 85; // Schneeschauer

    if (owm_id >= 200 && owm_id <= 202) return 95;  // Gewitter
    if (owm_id >= 210 && owm_id <= 221) return 96;
    if (owm_id >= 230 && owm_id <= 232) return 99;  // Gewitter mit Hagel

    return 3; // Fallback: bedeckt
}

static bool parse_current_item(cJSON *item, current_weather_data_t *out) {
    double temp, pressure, humidity, clouds, wind_speed, wind_deg;
    int weather_id;

    if (!req_num(item, "temp", &temp) ||
        !req_num(item, "pressure", &pressure) ||
        !req_num(item, "humidity", &humidity) ||
        !req_num(item, "clouds", &clouds) ||
        !req_num(item, "wind_speed", &wind_speed) ||
        !req_num(item, "wind_deg", &wind_deg) ||
        !owm_weather_id(item, &weather_id)) {
        ESP_LOGE(TAG, "Current weather data incomplete - missing required fields");
        return false;
    }

    out->temperature_2m = temp;
    out->dew_point_2m = opt_num(item, "dew_point", temp);
    out->relative_humidity_2m = (int)humidity;
    out->pressure_msl = pressure;
    out->apparent_temperature = opt_num(item, "feels_like", temp);
    out->is_day = owm_is_day(item) ? 1 : 0;
    out->weather_code = owm_weather_id_to_wmo(weather_id);
    out->cloud_cover = (int)clouds;
    out->wind_speed_10m = wind_speed * 3.6;
    out->wind_direction_10m = (int)wind_deg;
    // Boen fehlen bei OWM manchmal (z.B. Windstille) - dann Windgeschwindigkeit
    // als Boe uebernehmen statt das Feld leer zu lassen
    out->wind_gusts_10m = opt_num(item, "wind_gust", wind_speed) * 3.6;
    out->uv_index = opt_num(item, "uvi", 0.0);

    return true;
}

static bool parse_hourly_item(cJSON *item, hourly_weather_data_t *out) {
    double dt, temp, clouds, wind_speed;

    if (!req_num(item, "dt", &dt) ||
        !req_num(item, "temp", &temp) ||
        !req_num(item, "clouds", &clouds) ||
        !req_num(item, "wind_speed", &wind_speed)) {
        ESP_LOGE(TAG, "Hourly data item invalid - missing required fields");
        return false;
    }

    bool is_day = owm_is_day(item);
    time_t ts = (time_t)dt;
    localtime_r(&ts, &out->time);

    out->temperature_2m = temp;
    out->dew_point_2m = opt_num(item, "dew_point", temp);
    out->precipitation_probability = opt_num(item, "pop", 0.0) * 100.0;
    out->rain = opt_nested_num(item, "rain", "1h", 0.0);
    out->showers = 0.0; // OWM unterscheidet keine Schauer von normalem Regen
    out->snowfall = opt_nested_num(item, "snow", "1h", 0.0) / 10.0; // mm -> cm
    out->wind_speed_10m = wind_speed * 3.6;
    // Boen fehlen bei OWM manchmal (z.B. Windstille) - dann Windgeschwindigkeit
    // als Boe uebernehmen statt das Feld leer zu lassen
    out->wind_gusts_10m = opt_num(item, "wind_gust", wind_speed) * 3.6;
    out->cloud_cover = clouds;
    out->is_day = is_day;
    // OWM liefert keine Sonnenscheindauer - Naeherung ueber Bewoelkung
    out->sunshine_duration = is_day ? (100.0 - clouds) / 100.0 * 3600.0 : 0.0;

    return true;
}

static bool parse_daily_item(cJSON *item, daily_weather_data_t *out) {
    double dt, sunrise_ts, sunset_ts, temp_max, temp_min, clouds, wind_speed;

    if (!req_num(item, "dt", &dt) ||
        !req_num(item, "sunrise", &sunrise_ts) ||
        !req_num(item, "sunset", &sunset_ts) ||
        !req_nested_num(item, "temp", "max", &temp_max) ||
        !req_nested_num(item, "temp", "min", &temp_min) ||
        !req_num(item, "clouds", &clouds) ||
        !req_num(item, "wind_speed", &wind_speed)) {
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
    // OWM liefert keine Sonnenscheindauer - Naeherung ueber Bewoelkung
    out->sunshine_duration = (100.0 - clouds) / 100.0 * daylight_duration;
    // Anders als bei current/hourly ist "rain"/"snow" beim Daily-Endpunkt ein
    // einfacher Zahlenwert (Tagessumme), kein verschachteltes {"1h": ...}.
    out->rain_sum = opt_num(item, "rain", 0.0);
    out->showers_sum = 0.0;
    out->snowfall_sum = opt_num(item, "snow", 0.0) / 10.0; // mm -> cm
    out->precipitation_probability_max = opt_num(item, "pop", 0.0) * 100.0;
    out->wind_speed_10m_max = wind_speed * 3.6;
    // Boen fehlen bei OWM manchmal (z.B. Windstille) - dann Windgeschwindigkeit
    // als Boe uebernehmen statt das Feld leer zu lassen
    out->wind_gusts_10m_max = opt_num(item, "wind_gust", wind_speed) * 3.6;

    return true;
}

bool openweathermap_fetch_current(esp_http_client_handle_t client, weather_http_response_t *response,
                                   const char *latitude, const char *longitude, const char *api_key,
                                   current_weather_data_t *out) {
    char url[512];
    snprintf(url, sizeof(url), OWM_URL_CURRENT, latitude, longitude, api_key);
    ESP_LOGI(TAG, "Call current weather API (OWM): %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsArray(data) || cJSON_GetArraySize(data) < 1) {
        ESP_LOGE(TAG, "Missing or invalid 'data' array in current response");
    } else {
        success = parse_current_item(cJSON_GetArrayItem(data, 0), out);
        if (success) {
            ESP_LOGI(TAG, "Current weather data successfully retrieved");
        }
    }

    cJSON_Delete(json);
    return success;
}

bool openweathermap_fetch_hourly(esp_http_client_handle_t client, weather_http_response_t *response,
                                  const char *latitude, const char *longitude, const char *api_key,
                                  hourly_weather_data_t *out, int count) {
    char url[512];
    snprintf(url, sizeof(url), OWM_URL_HOURLY, latitude, longitude, api_key);

    int collected = 0;
    for (int page = 0; page < 4 && collected < count; page++) {
        ESP_LOGI(TAG, "Call hourly weather API (OWM), page %d: %s", page, url);

        cJSON *json = weather_http_get_json(client, response, url);
        if (json == NULL) {
            return false;
        }

        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (!data || !cJSON_IsArray(data)) {
            ESP_LOGE(TAG, "Missing or invalid 'data' array in hourly response");
            cJSON_Delete(json);
            return false;
        }

        int n = cJSON_GetArraySize(data);
        for (int i = 0; i < n && collected < count; i++) {
            if (!parse_hourly_item(cJSON_GetArrayItem(data, i), &out[collected])) {
                cJSON_Delete(json);
                return false;
            }
            collected++;
        }

        cJSON *next = cJSON_GetObjectItem(json, "next");
        bool has_next = next && cJSON_IsString(next) && next->valuestring[0] != '\0';
        if (has_next) {
            strncpy(url, next->valuestring, sizeof(url) - 1);
            url[sizeof(url) - 1] = '\0';
        }
        cJSON_Delete(json);

        if (!has_next) {
            break;
        }
    }

    if (collected < count) {
        ESP_LOGE(TAG, "Insufficient hourly data: need %d items, got %d", count, collected);
        return false;
    }

    ESP_LOGI(TAG, "Hourly weather data successfully retrieved");
    return true;
}

bool openweathermap_fetch_daily(esp_http_client_handle_t client, weather_http_response_t *response,
                                 const char *latitude, const char *longitude, const char *api_key,
                                 daily_weather_data_t *out, int count) {
    char url[512];
    snprintf(url, sizeof(url), OWM_URL_DAILY, latitude, longitude, api_key, count);
    ESP_LOGI(TAG, "Call daily weather API (OWM): %s", url);

    cJSON *json = weather_http_get_json(client, response, url);
    if (json == NULL) {
        return false;
    }

    bool success = false;
    cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsArray(data)) {
        ESP_LOGE(TAG, "Missing or invalid 'data' array in daily response");
    } else if (cJSON_GetArraySize(data) < count) {
        ESP_LOGE(TAG, "Insufficient daily data: need %d days, have %d", count, cJSON_GetArraySize(data));
    } else {
        success = true;
        for (int i = 0; i < count && success; i++) {
            success = parse_daily_item(cJSON_GetArrayItem(data, i), &out[i]);
        }
        if (success) {
            ESP_LOGI(TAG, "Daily weather data successfully retrieved");
        }
    }

    cJSON_Delete(json);
    return success;
}
