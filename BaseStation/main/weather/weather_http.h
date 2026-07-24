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

/* Legt einen esp_http_client an, dessen Event-Handler die Antwort in
 * `response` sammelt (SPIRAM-Buffer, wird bei jedem Call in
 * weather_http_get_json() neu befuellt). `response` muss so lange leben wie
 * der Client. */
esp_http_client_handle_t weather_http_client_create(weather_http_response_t *response);

/* Fuehrt ein GET auf `url` aus und parst den Body als JSON. Gibt bei
 * Transport- oder Parse-Fehler NULL zurueck. Der interne Response-Buffer
 * wird in jedem Fall (Erfolg wie Fehler) freigegeben, bevor die Funktion
 * zurueckkehrt - der Aufrufer muss das zurueckgegebene cJSON* mit
 * cJSON_Delete() freigeben. */
cJSON *weather_http_get_json(esp_http_client_handle_t client, weather_http_response_t *response, const char *url);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_HTTP_H */
