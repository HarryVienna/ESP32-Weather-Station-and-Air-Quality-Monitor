#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "openweathermap_provider.h"

static const char *TAG = "openweathermap_provider";

/* Days since 1970-01-01 for a proleptic Gregorian civil date (Howard
 * Hinnant's algorithm - http://howardhinnant.github.io/date_algorithms.html).
 * Used below to build a UTC-midnight timestamp straight from a Y/M/D triple
 * without needing timegm(), which picolibc doesn't provide. */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

/* One Call API 4.0 (https://openweathermap.org/api/one-call-4) - unlike
 * 3.0, current/hourly/daily are separate endpoints, no combined call.
 * Responses are {"data": [...], "next": "...", "prev": "..."} - hourly
 * returns at most 20 entries per response, hence pagination via "next". */
static const char *OWM_URL_CURRENT =
        "https://api.openweathermap.org/data/4.0/onecall/current?"
        "lat=%s&lon=%s&appid=%s&units=metric";

static const char *OWM_URL_HOURLY =
        "https://api.openweathermap.org/data/4.0/onecall/timeline/1h?"
        "lat=%s&lon=%s&appid=%s&units=metric&cnt=20";

static const char *OWM_URL_DAILY =
        "https://api.openweathermap.org/data/4.0/onecall/timeline/1day?"
        "lat=%s&lon=%s&appid=%s&units=metric&cnt=%d&start=%lld";

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

/* is_day isn't in any field directly - OWM appends a 'd' or 'n' to the
 * icon code (e.g. "01d"/"01n"). If the icon is missing, day is assumed. */
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

/* Rough mapping of OWM condition codes onto the 28 existing WMO icons
 * (see icon_mapping_day/night in gui_weather.c). */
static int owm_weather_id_to_wmo(int owm_id) {
    if (owm_id == 800) return 0;   // clear sky
    if (owm_id == 801) return 1;   // few clouds
    if (owm_id == 802) return 2;   // scattered clouds
    if (owm_id == 803 || owm_id == 804) return 3; // cloudy/overcast

    if (owm_id >= 701 && owm_id <= 771) return 45; // fog/haze/sand/smoke

    if (owm_id == 300 || owm_id == 310) return 51; // light drizzle
    if (owm_id == 301 || owm_id == 311 || owm_id == 321) return 53;
    if (owm_id == 302 || owm_id == 312 || owm_id == 313 || owm_id == 314) return 55;

    if (owm_id == 500) return 61;  // light rain
    if (owm_id == 501) return 63;  // moderate rain
    if (owm_id == 502 || owm_id == 503 || owm_id == 504) return 65; // heavy rain
    if (owm_id == 511) return 66;  // freezing rain

    if (owm_id == 520) return 80;  // light rain shower
    if (owm_id == 521) return 81;
    if (owm_id == 522 || owm_id == 531) return 82;

    if (owm_id == 600 || owm_id == 612 || owm_id == 615 || owm_id == 620) return 71; // light snow
    if (owm_id == 601) return 73;
    if (owm_id == 602 || owm_id == 622) return 75;
    if (owm_id == 611 || owm_id == 613 || owm_id == 616 || owm_id == 621) return 85; // snow shower

    if (owm_id >= 200 && owm_id <= 202) return 95;  // thunderstorm
    if (owm_id >= 210 && owm_id <= 221) return 96;
    if (owm_id >= 230 && owm_id <= 232) return 99;  // thunderstorm with hail

    return 3; // fallback: overcast
}

static bool parse_current_item(cJSON *item, current_weather_data_t *out) {
    double temp, pressure, humidity, clouds, wind_speed, wind_deg, sunrise_ts, sunset_ts;
    int weather_id;

    if (!req_num(item, "temp", &temp) ||
        !req_num(item, "pressure", &pressure) ||
        !req_num(item, "humidity", &humidity) ||
        !req_num(item, "clouds", &clouds) ||
        !req_num(item, "wind_speed", &wind_speed) ||
        !req_num(item, "wind_deg", &wind_deg) ||
        !req_num(item, "sunrise", &sunrise_ts) ||
        !req_num(item, "sunset", &sunset_ts) ||
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
    // Gusts are sometimes missing from OWM (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m = opt_num(item, "wind_gust", wind_speed) * 3.6;
    out->uv_index = opt_num(item, "uvi", 0.0);

    // Unlike "dt" on the daily endpoint, these are genuine moments in time
    // (not a pure calendar day), so a real local-time conversion is right here.
    time_t sunrise_time = (time_t)sunrise_ts;
    localtime_r(&sunrise_time, &out->sunrise);
    time_t sunset_time = (time_t)sunset_ts;
    localtime_r(&sunset_time, &out->sunset);

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
    out->showers = 0.0; // OWM doesn't distinguish showers from regular rain
    out->snowfall = opt_nested_num(item, "snow", "1h", 0.0) / 10.0; // mm -> cm
    out->wind_speed_10m = wind_speed * 3.6;
    // Gusts are sometimes missing from OWM (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m = opt_num(item, "wind_gust", wind_speed) * 3.6;
    out->cloud_cover = clouds;
    out->is_day = is_day;
    // OWM doesn't provide sunshine duration - approximated via cloud cover
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

    // "dt" is a pure calendar day, not a specific moment - openweathermap_fetch_daily()
    // builds "start" from the location's local today's own Y/M/D read directly as a UTC
    // timestamp (no local->UTC conversion), and OWM echoes that back verbatim as day 0,
    // +1 day per subsequent entry. Reading it back with gmtime_r() (not localtime_r()) is
    // the matching other half of that trick: it returns those same Y/M/D digits untouched,
    // which are already the location's local calendar date - no TZ conversion needed or
    // wanted here. (localtime_r() would shift the date by the UTC offset and often land on
    // the wrong day - see the "Di statt Mi" investigation.)
    time_t dt_ts = (time_t)dt;
    gmtime_r(&dt_ts, &out->time);

    out->temperature_2m_max = temp_max;
    out->temperature_2m_min = temp_min;
    out->daylight_duration = daylight_duration;
    // OWM doesn't provide sunshine duration - approximated via cloud cover
    out->sunshine_duration = (100.0 - clouds) / 100.0 * daylight_duration;
    // Unlike current/hourly, "rain"/"snow" on the daily endpoint is a
    // plain number (daily total), not a nested {"1h": ...}.
    out->rain_sum = opt_num(item, "rain", 0.0);
    out->showers_sum = 0.0;
    out->snowfall_sum = opt_num(item, "snow", 0.0) / 10.0; // mm -> cm
    out->precipitation_probability_max = opt_num(item, "pop", 0.0) * 100.0;
    out->wind_speed_10m_max = wind_speed * 3.6;
    // Gusts are sometimes missing from OWM (e.g. calm winds) - in that
    // case use wind speed as the gust value instead of leaving it empty
    out->wind_gusts_10m_max = opt_num(item, "wind_gust", wind_speed) * 3.6;

    return true;
}

bool openweathermap_fetch_current(esp_http_client_handle_t client, http_response_t *response,
                                   const char *latitude, const char *longitude, const char *api_key,
                                   current_weather_data_t *out) {
    char url[512];
    snprintf(url, sizeof(url), OWM_URL_CURRENT, latitude, longitude, api_key);
    ESP_LOGI(TAG, "Call current weather API (OWM): %s", url);

    cJSON *json = http_get_json(client, response, url);
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

bool openweathermap_fetch_hourly(esp_http_client_handle_t client, http_response_t *response,
                                  const char *latitude, const char *longitude, const char *api_key,
                                  hourly_weather_data_t *out, int count) {
    char url[512];
    snprintf(url, sizeof(url), OWM_URL_HOURLY, latitude, longitude, api_key);

    int collected = 0;
    for (int page = 0; page < 4 && collected < count; page++) {
        ESP_LOGI(TAG, "Call hourly weather API (OWM), page %d: %s", page, url);

        cJSON *json = http_get_json(client, response, url);
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
            const char *next_url = next->valuestring;
            /* OWM's "next" pagination link sometimes comes back as plain
             * http:// even though the API is HTTPS-only. esp_http_client
             * only re-detects the transport (plain/TLS) when the host or
             * port changes on set_url(), not the scheme, so a bare http://
             * here would reuse the still-open TLS transport from the
             * previous page and fail the handshake against port 80. Force
             * https so the same client handle can be reused across pages. */
            if (strncmp(next_url, "http://", 7) == 0) {
                snprintf(url, sizeof(url), "https://%s", next_url + 7);
            } else {
                strncpy(url, next_url, sizeof(url) - 1);
                url[sizeof(url) - 1] = '\0';
            }
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

bool openweathermap_fetch_daily(esp_http_client_handle_t client, http_response_t *response,
                                 const char *latitude, const char *longitude, const char *api_key,
                                 daily_weather_data_t *out, int count) {
    char url[512];

    // OWM's daily endpoint only returns data starting from "today" as
    // documented if "start" is an exact UTC-midnight timestamp (a multiple
    // of 86400) - verified empirically: a non-aligned value (e.g. local
    // midnight straight-converted to UTC, which lands on a fractional UTC
    // hour for any non-zero offset) makes it silently fall back to
    // unrelated/stale data instead.
    //
    // So: take the location's local today's own Y/M/D and build a UTC
    // timestamp directly from those digits (days_from_civil(), no TZ
    // conversion applied) - that's always UTC-midnight-aligned by
    // construction, for any timezone. OWM then echoes it back verbatim as
    // day 0, +1 UTC day per subsequent entry (verified). parse_daily_item()
    // reads "dt" back with gmtime_r() rather than localtime_r() - the
    // matching other half of this trick: it returns those same Y/M/D
    // digits untouched, which are already the local calendar date, so no
    // further TZ conversion is needed (or wanted) at that end either.
    time_t now;
    time(&now);
    struct tm local_now;
    localtime_r(&now, &local_now);

    int64_t days = days_from_civil(local_now.tm_year + 1900, local_now.tm_mon + 1, local_now.tm_mday);
    time_t start_ts = (time_t)(days * 86400);

    snprintf(url, sizeof(url), OWM_URL_DAILY, latitude, longitude, api_key, count, (long long)start_ts);
    ESP_LOGI(TAG, "Call daily weather API (OWM): %s", url);

    cJSON *json = http_get_json(client, response, url);
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
