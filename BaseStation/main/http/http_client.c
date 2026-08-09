#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

#include "http_client.h"

static const char *TAG = "http_client";

static void *cjson_spiram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
}

static void cjson_spiram_free(void *ptr) {
    heap_caps_free(ptr);
}

// cJSON's default hooks use plain malloc/free, which - below
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL - allocates from internal DRAM. A
// parsed response is a tree of many small nodes, so doing this repeatedly
// (weather polling, OTA release checks) would slowly fragment the small,
// shared internal heap instead of the much larger SPIRAM pool.
static void http_ensure_json_allocator(void) {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    cJSON_Hooks hooks = {
        .malloc_fn = cjson_spiram_malloc,
        .free_fn = cjson_spiram_free,
    };
    cJSON_InitHooks(&hooks);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_response_t *response = (http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response->buffer == NULL) {
                char *new_buffer = (char *)heap_caps_malloc(evt->data_len + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                if (new_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate %d bytes for HTTP response buffer", evt->data_len + 1);
                    return ESP_FAIL;
                }
                response->buffer = new_buffer;
                response->buffer_len = evt->data_len;
                memcpy(response->buffer, evt->data, evt->data_len);
            } else {
                char *new_buffer = (char *)heap_caps_realloc(response->buffer, response->buffer_len + evt->data_len + 1,
                                                               MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                if (new_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to grow HTTP response buffer to %d bytes",
                             response->buffer_len + evt->data_len + 1);
                    heap_caps_free(response->buffer);
                    response->buffer = NULL;
                    response->buffer_len = 0;
                    return ESP_FAIL;
                }
                response->buffer = new_buffer;
                memcpy(response->buffer + response->buffer_len, evt->data, evt->data_len);
                response->buffer_len += evt->data_len;
            }
            response->buffer[response->buffer_len] = 0; // Null-terminate the buffer
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_http_client_handle_t http_client_create(http_response_t *response) {
    http_ensure_json_allocator();

    esp_http_client_config_t config = {
        .event_handler = http_event_handler,
        .url = "https://127.0.0.1/",  // Dummy URL
        .is_async = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = response,
        .disable_auto_redirect = true,
    };

    return esp_http_client_init(&config);
}

cJSON *http_get_json(esp_http_client_handle_t client, http_response_t *response, const char *url) {
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
