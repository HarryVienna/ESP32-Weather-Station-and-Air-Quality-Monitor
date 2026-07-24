#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

#include "weather_http.h"

static const char *TAG = "weather_http";

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt) {
    weather_http_response_t *response = (weather_http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response->buffer == NULL) {
                response->buffer_len = evt->data_len;
                response->buffer = (char *)heap_caps_malloc(response->buffer_len + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                memcpy(response->buffer, evt->data, evt->data_len);
            } else {
                response->buffer_len += evt->data_len;
                response->buffer = (char *)heap_caps_realloc(response->buffer, response->buffer_len + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                memcpy(response->buffer + response->buffer_len - evt->data_len, evt->data, evt->data_len);
            }
            response->buffer[response->buffer_len] = 0; // Null-terminate the buffer
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_http_client_handle_t weather_http_client_create(weather_http_response_t *response) {
    esp_http_client_config_t config = {
        .event_handler = weather_http_event_handler,
        .url = "https://api.open-meteo.com/v1/forecast",
        .is_async = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = response,
        .disable_auto_redirect = true,
    };

    return esp_http_client_init(&config);
}

cJSON *weather_http_get_json(esp_http_client_handle_t client, weather_http_response_t *response, const char *url) {
    esp_http_client_set_url(client, url);

    esp_err_t err = esp_http_client_perform(client);

    cJSON *json = NULL;

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64, esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));

        json = cJSON_Parse(response->buffer);
        if (json == NULL) {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL) {
                ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    if (response->buffer) {
        heap_caps_free(response->buffer);
        response->buffer = NULL;
        response->buffer_len = 0;
    }

    esp_http_client_close(client);

    return json;
}
