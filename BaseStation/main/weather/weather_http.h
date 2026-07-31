#ifndef WEATHER_HTTP_H
#define WEATHER_HTTP_H

#include "esp_http_client.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *buffer;
    int buffer_len;
} weather_http_response_t;

/* Creates an esp_http_client whose event handler collects the response
 * into `response` (SPIRAM buffer, refilled on every call in
 * weather_http_get_json()). `response` must live as long as the client. */
esp_http_client_handle_t weather_http_client_create(weather_http_response_t *response);

/* Performs a GET on `url` and parses the body as JSON. Returns NULL on a
 * transport or parse error. The internal response buffer is freed in
 * every case (success as well as failure) before the function returns -
 * the caller must free the returned cJSON* with cJSON_Delete(). */
cJSON *weather_http_get_json(esp_http_client_handle_t client, weather_http_response_t *response, const char *url);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_HTTP_H */
