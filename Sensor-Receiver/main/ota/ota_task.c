/**
 * @file ota_task.c
 * @brief Receiver firmware update: WiFi connection on the P4's command + esp_https_ota
 *
 * The S3 has no WiFi setup UI of its own and no permanent internet access
 * (it normally lives exclusively on the fixed ESP-NOW channel, see
 * network/esp-now.c). An update therefore runs entirely on the P4's
 * command (see I2C_REG_SET_WIFI_SSID/_PASS/_OTA_START in i2c/i2c_slave.c):
 *
 *   1. P4 sends SSID + password (kept in RAM only, never written to NVS)
 *   2. P4 sends OTA_START -> ota_task_trigger() starts this task
 *   3. Pause ESP-NOW (the WiFi radio is needed for the AP connection)
 *   4. Connect to the AP (same WiFi network as the P4)
 *   5. Query the latest GitHub release, download the "Receiver.bin" asset
 *      and flash it (exactly like BaseStation/main/ota/ota_task.c for the
 *      P4, just a different asset name)
 *   6. Success -> reboot. Failure -> re-enable ESP-NOW (esp_now_resume()),
 *      set status to FAILED.
 */

#include "ota_task.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "network/esp-now.h"
#include "display/display.h"

static const char *TAG = "receiver_ota";

/* Same repo as BaseStation/main/ota/ota_task.c - own asset name so a
 * single release can carry both binaries. */
#define GITHUB_OWNER          "HarryVienna"
#define GITHUB_REPO           "ESP32-Weather-Station-and-Air-Quality-Monitor"
#define GITHUB_RELEASE_URL    "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest"
#define OTA_ASSET_NAME        "Receiver.bin"

#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1
#define WIFI_CONNECT_MAX_RETRIES  5
#define WIFI_CONNECT_TIMEOUT_MS   (20 * 1000)

/* wifi_config_t.sta.ssid/.password are 32/64 bytes, not necessarily NUL-
 * terminated - our copies are (one byte more of buffer). */
static char s_ssid[33];
static char s_password[65];

static volatile ota_task_status_t s_status = OTA_TASK_STATUS_IDLE;

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* -------------------------------------------------------------------------- */

void ota_task_set_wifi_ssid(const char *ssid)
{
    memset(s_ssid, 0, sizeof(s_ssid));
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
}

void ota_task_set_wifi_password(const char *password)
{
    memset(s_password, 0, sizeof(s_password));
    strncpy(s_password, password, sizeof(s_password) - 1);
}

/* -------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_CONNECT_MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_retry_num, WIFI_CONNECT_MAX_RETRIES);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Connects blocking (bounded) to the AP configured via
 * ota_task_set_wifi_*(). WiFi is already running in STA mode (see
 * network/esp-now.c: init_wifi()) - this only reconfigures and reconnects,
 * no need to call esp_wifi_start() again. Named to match
 * BaseStation/main/wifi/network.c: wifi_connect(). */
static bool wifi_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t inst_any = NULL, inst_ip = NULL;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip);

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password));

    s_retry_num = 0;
    bool connected = false;

    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) == ESP_OK &&
        esp_wifi_connect() == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                pdFALSE, pdFALSE,
                                                pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
        connected = (bits & WIFI_CONNECTED_BIT) != 0;
    } else {
        ESP_LOGE(TAG, "esp_wifi_set_config/connect failed");
    }

    /* Unlike BaseStation/main/wifi/network.c: wifi_connect() - which sets
     * up its event group/handler once in wifi_init() and keeps them for
     * the app's entire lifetime, because that connection IS the permanent
     * one (auto-reconnect, status icon) - this connection only exists for
     * the duration of this one OTA download. wifi_event_handler() has no
     * job once this function returns (esp_now_resume()/a reboot take over
     * right after), so it must be torn down here, or every future OTA
     * attempt would register another instance of it, each firing on every
     * future WiFi event. */
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, inst_any);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, inst_ip);
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;

    return connected;
}

/* -------------------------------------------------------------------------- */

/* Minimal HTTP GET + JSON parse, analogous to
 * BaseStation/main/http/http_client.c - deliberately self-contained
 * here (no SPIRAM assumed on the S3, no shared cross-project module),
 * since the GitHub release response is small and only fetched once per
 * update run. */
typedef struct {
    char   *buffer;
    size_t  buffer_len;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;

    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    char *new_buffer = (char *)realloc(response->buffer, response->buffer_len + evt->data_len + 1);
    if (new_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to grow HTTP response buffer to %d bytes",
                 (int)(response->buffer_len + evt->data_len + 1));
        return ESP_FAIL;
    }
    response->buffer = new_buffer;
    memcpy(response->buffer + response->buffer_len, evt->data, evt->data_len);
    response->buffer_len += evt->data_len;
    response->buffer[response->buffer_len] = '\0';

    return ESP_OK;
}

/* Searches the "assets" array of the release JSON for the entry with
 * name == OTA_ASSET_NAME and returns its browser_download_url (ownership
 * stays with the passed cJSON tree). */
static const char *find_asset_download_url(cJSON *release_json)
{
    cJSON *assets = cJSON_GetObjectItem(release_json, "assets");
    if (!assets || !cJSON_IsArray(assets)) {
        return NULL;
    }

    int n = cJSON_GetArraySize(assets);
    for (int i = 0; i < n; i++) {
        cJSON *asset = cJSON_GetArrayItem(assets, i);
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (name && cJSON_IsString(name) && strcmp(name->valuestring, OTA_ASSET_NAME) == 0) {
            cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
            if (url && cJSON_IsString(url)) {
                return url->valuestring;
            }
        }
    }
    return NULL;
}

/* Downloads the firmware image from download_url into the inactive OTA
 * partition and reboots on success. Only returns on failure. */
static bool apply_update(const char *download_url)
{
    ESP_LOGI(TAG, "Updating receiver firmware from %s", download_url);

    esp_http_client_config_t http_config = {
        .url                = download_url,
        .crt_bundle_attach  = esp_crt_bundle_attach,
        .buffer_size        = 4096,
        .buffer_size_tx     = 4096,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(ret));
        return false;
    }

    do {
        ret = esp_https_ota_perform(ota_handle);
    } while (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(ota_handle);
        return false;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "Incomplete OTA image received");
        esp_https_ota_abort(ota_handle);
        return false;
    }

    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Receiver OTA update successful, rebooting");
    esp_restart();
    /* unreachable */
    return true;
}

/* Queries the current GitHub release and flashes "Receiver.bin" if
 * present. Only returns on failure. */
static bool download_and_flash(void)
{
    http_response_t response = {0};

    esp_http_client_config_t config = {
        .url                 = GITHUB_RELEASE_URL,
        .event_handler       = http_event_handler,
        .crt_bundle_attach   = esp_crt_bundle_attach,
        .user_data           = &response,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "Receiver-OTA");

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch latest release info: %s", esp_err_to_name(err));
        free(response.buffer);
        return false;
    }

    cJSON *json = cJSON_Parse(response.buffer);
    free(response.buffer);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse release JSON");
        return false;
    }

    const char *download_url = find_asset_download_url(json);
    if (download_url == NULL) {
        ESP_LOGW(TAG, "Latest release has no '%s' asset", OTA_ASSET_NAME);
        cJSON_Delete(json);
        return false;
    }

    cJSON *tag = cJSON_GetObjectItem(json, "tag_name");
    if (tag && cJSON_IsString(tag)) {
        char line[DISPLAY_LINE_MAX_LEN];
        snprintf(line, sizeof(line), "Update to %s", tag->valuestring);
        display_log_line(line);
    }

    /* download_url points into json - don't delete before the last use. */
    char url_copy[512];
    strncpy(url_copy, download_url, sizeof(url_copy) - 1);
    url_copy[sizeof(url_copy) - 1] = '\0';
    cJSON_Delete(json);

    return apply_update(url_copy);
}

/* -------------------------------------------------------------------------- */

static void ota_run_task(void *arg)
{
    bool ok = wifi_connect();

    if (ok) {
        ok = download_and_flash();
        /* apply_update() only returns on failure (success -> esp_restart()) */
    } else {
        ESP_LOGE(TAG, "Could not connect to '%s' for receiver OTA", s_ssid);
    }

    if (!ok) {
        s_status = OTA_TASK_STATUS_FAILED;
        esp_wifi_disconnect();
        esp_now_resume();
    }

    vTaskDelete(NULL);
}

void ota_task_trigger(void)
{
    if (s_status == OTA_TASK_STATUS_IN_PROGRESS) {
        ESP_LOGW(TAG, "OTA already in progress, ignoring trigger");
        return;
    }

    s_status = OTA_TASK_STATUS_IN_PROGRESS;
    esp_now_pause();

    xTaskCreate(ota_run_task, "receiver_ota", 8192, NULL, 5, NULL);
}
