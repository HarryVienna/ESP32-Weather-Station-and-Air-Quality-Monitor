#include "ota_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http/http_client.h"
#include "gui/ota/gui_ota.h"
#include "receiver/receiver.h"
#include "nvs/preferences.h"

static const char *TAG = "ota_task";

/* Between finding an update (check_for_update(), OTA task) and actually
 * installing it (button click on the LVGL task, see
 * ota_task_install_available_update()) there's a user confirmation -
 * download_url has to wait somewhere until then. Only one update can be
 * pending at a time. */
static char s_pending_download_url[512];

/* Suspends the OTA task instead of periodically polling for nothing while
 * waiting for user confirmation (see check_for_update()) - woken back up
 * by install_task() on failure (on success the device reboots anyway, see
 * apply_update()). */
static TaskHandle_t s_ota_task_handle = NULL;

/* Repo that the release workflow (.github/workflows/release.yml) attaches
 * the firmware binary to as a release asset. */
#define GITHUB_OWNER          "HarryVienna"
#define GITHUB_REPO           "ESP32-Weather-Station-and-Air-Quality-Monitor"
#define GITHUB_RELEASE_URL    "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest"
#define OTA_ASSET_NAME        "Basestation.bin"

/* First check shortly after task start, then every 24h - GitHub releases
 * aren't frequent enough to justify more frequent polling. */
#define OTA_FIRST_CHECK_DELAY_MS   (1000 * 30)
#define OTA_CHECK_INTERVAL_MS      (1000 * 60 * 60 * 1)

/* Searches the "assets" array of the GitHub release JSON for the entry
 * with name == OTA_ASSET_NAME and returns its browser_download_url
 * (ownership stays with the passed cJSON tree, don't free it yourself). */
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

/* Sends the receiver its WiFi SSID/password + start command via I2C - see
 * apply_update() below: always runs alongside it whenever the P4 updates
 * itself, without any version check of its own for the receiver. The S3
 * downloads its own "Receiver.bin" from the same GitHub tag and falls
 * back to ESP-NOW on its own if the release doesn't contain one (see
 * Sensor-Receiver/main/ota/ota_task.c) - so deliberately no asset
 * pre-check here. Fire-and-forget, doesn't block the P4's reboot. */
static void trigger_receiver_update(void)
{
    /* Same source as the setup-screen WiFi connection (see
     * gui/setup/gui_setup_network_actions.c) - no separate credential
     * storage for the receiver, or the two could drift apart. */
    nvs_handle_t nvs_handle;
    if (nvs_open("weatherstation", NVS_READONLY, &nvs_handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not open NVS for WiFi credentials - skipping receiver OTA");
        return;
    }
    char *ssid = get_string_from_nvs(nvs_handle, "ssid", "");
    char *password = get_string_from_nvs(nvs_handle, "password", "");
    nvs_close(nvs_handle);

    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "No WiFi SSID stored - cannot start receiver OTA");
    } else {
        ESP_LOGI(TAG, "Triggering receiver OTA alongside own update");
        receiver_start_ota(ssid, password);
    }

    free(ssid);
    free(password);
}

/* Downloads the firmware binary from download_url into the inactive OTA
 * partition and reboots on success. Only returns on failure (and then
 * switches back to the Weatherstation screen instead of getting stuck on
 * "Upgrading...").
 *
 * Deliberately uses the granular esp_https_ota_begin()/_perform()/_finish()
 * API instead of the simple esp_https_ota() - only this way can you get
 * esp_https_ota_get_image_len_read()/_get_image_size() along the way for
 * the progress bar (see gui/ota/gui_ota.c, no direct LVGL access here). */
static void apply_update(const char *download_url)
{
    ESP_LOGI(TAG, "Updating firmware from %s", download_url);

    gui_ota_update_started();

    esp_http_client_config_t http_config = {
        .url             = download_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* Auto-redirect deliberately NOT disabled: GitHub asset downloads
         * redirect to objects.githubusercontent.com with a long signed
         * query string (AWS-style signature parameters). The 512-byte
         * default buffer (DEFAULT_HTTP_BUF_SIZE) isn't enough for that -
         * neither for reading the redirect headers (buffer_size) nor for
         * building the outgoing request line for the redirected request,
         * which contains that same long query string (buffer_size_tx, see
         * http_client_prepare_first_line() in esp_http_client.c - this was
         * the actual source of "HTTP_CLIENT: Out of buffer"). Both are
         * therefore increased. */
        .buffer_size     = 4096,
        .buffer_size_tx  = 4096,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    int image_size = esp_https_ota_get_image_size(ota_handle);

    do {
        ret = esp_https_ota_perform(ota_handle);

        int bytes_read = esp_https_ota_get_image_len_read(ota_handle);
        if (image_size > 0 && bytes_read >= 0) {
            gui_ota_update_progress((int32_t)((int64_t)bytes_read * 100 / image_size));
        }
    } while (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(ota_handle);
        goto fail;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "Incomplete OTA image received");
        esp_https_ota_abort(ota_handle);
        goto fail;
    }

    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    gui_ota_update_progress(100);

    trigger_receiver_update();

    ESP_LOGI(TAG, "OTA update successful, rebooting");
    esp_restart();

fail:
    ESP_LOGE(TAG, "OTA update failed");
    gui_ota_update_failed();
}

/* Own task for apply_update(): the caller (ota_task_install_available_
 * update()) runs on the LVGL task (button click), but apply_update() is a
 * minutes-long blocking download - calling it directly would freeze the
 * entire UI (rendering, touch) for the duration of the download. */
static void install_task(void *pvParameter)
{
    apply_update(s_pending_download_url);
    /* apply_update() only returns on failure (success -> esp_restart()) -
     * wake the OTA task back up so it resumes its periodic checking. */
    vTaskResume(s_ota_task_handle);
    vTaskDelete(NULL);
}

void ota_task_install_available_update(void)
{
    xTaskCreatePinnedToCore(
        install_task,
        "OTA Install Task",
        8192,
        NULL,
        1,
        NULL,
        1);
}

typedef struct {
    int  major, minor, patch;
    bool has_suffix; /* "-dirty" and/or "-<N>-g<hash>" after X.Y.Z */
} app_version_t;

/* Parses "[v]X.Y.Z[-arbitrary rest]" (the format produced by "git describe
 * --always --tags --dirty", see project.cmake - "-dirty" for uncommitted
 * changes, "-<N>-g<hash>" for commits after the last tag). Returns false
 * if even X.Y.Z can't be found (e.g. falling back to a bare commit hash
 * with no reachable tag) - no meaningful comparison is possible then. */
static bool parse_version(const char *version, app_version_t *out)
{
    if (*version == 'v' || *version == 'V') {
        version++;
    }

    int consumed = 0;
    if (sscanf(version, "%d.%d.%d%n", &out->major, &out->minor, &out->patch, &consumed) != 3) {
        return false;
    }
    out->has_suffix = (version[consumed] != '\0');
    return true;
}

/* Compares like semver (major/minor/patch decide first), with one
 * exception when the version is otherwise equal: a version WITH a suffix
 * ("-dirty" or "-<N>-g<hash>") counts as NEWER than the bare X.Y.Z it's
 * based on - in git history it actually comes after (more commits and/or
 * local changes on top). So e.g. 0.1.0 < 0.1.0-dirty < 0.2.0. Returns <0
 * if a<b, 0 if equal, >0 if a>b. */
static int compare_versions(const app_version_t *a, const app_version_t *b)
{
    return 1; // DEBUG
    if (a->major != b->major) return a->major - b->major;
    if (a->minor != b->minor) return a->minor - b->minor;
    if (a->patch != b->patch) return a->patch - b->patch;
    return (int)a->has_suffix - (int)b->has_suffix;
}

/* One check cycle: query the latest GitHub release, compare versions,
 * update if different. */
static void check_for_update(esp_http_client_handle_t client, http_response_t *response)
{
    const char *running_version = esp_app_get_description()->version;

    esp_http_client_set_header(client, "User-Agent", "BaseStation-OTA");

    cJSON *json = http_get_json(client, response, GITHUB_RELEASE_URL);
    if (json == NULL) {
        ESP_LOGW(TAG, "Failed to fetch latest release info");
        return;
    }

    cJSON *tag = cJSON_GetObjectItem(json, "tag_name");
    if (!tag || !cJSON_IsString(tag)) {
        ESP_LOGW(TAG, "Release info missing 'tag_name'");
        cJSON_Delete(json);
        return;
    }

    ESP_LOGI(TAG, "Running version: %s, latest release: %s", running_version, tag->valuestring);

    app_version_t running_v, latest_v;
    bool parsed = parse_version(running_version, &running_v) && parse_version(tag->valuestring, &latest_v);
    if (!parsed) {
        ESP_LOGW(TAG, "Could not parse version(s) for comparison - skipping");
        cJSON_Delete(json);
        return;
    }
    if (compare_versions(&latest_v, &running_v) <= 0) {
        cJSON_Delete(json);
        return;
    }

    const char *download_url = find_asset_download_url(json);
    if (download_url == NULL) {
        ESP_LOGW(TAG, "Release %s has no '%s' asset", tag->valuestring, OTA_ASSET_NAME);
        cJSON_Delete(json);
        return;
    }

    /* download_url and tag->valuestring point into json, so only free it
     * after the last access - gui_ota_update_available() copies the
     * version number itself (lv_label_set_text()), so it also still needs
     * to be called before that. */
    strncpy(s_pending_download_url, download_url, sizeof(s_pending_download_url) - 1);
    s_pending_download_url[sizeof(s_pending_download_url) - 1] = '\0';
    ESP_LOGI(TAG, "Update available (%s) - waiting for user confirmation", tag->valuestring);
    gui_ota_update_available(tag->valuestring);
    cJSON_Delete(json);

    /* Hangs here until install_task() (failure case) or a reboot (success)
     * wakes us back up - no reason to wake up periodically and re-check
     * in the meantime. */
    vTaskSuspend(NULL);
}

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Start OTA task");

    http_response_t response = {0};
    esp_http_client_handle_t client = http_client_create(&response);

    vTaskDelay(pdMS_TO_TICKS(OTA_FIRST_CHECK_DELAY_MS));

    for (;;) {
        check_for_update(client, &response);
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
    }
}

void ota_task_start(void)
{
    xTaskCreatePinnedToCore(
        ota_task,
        "OTA Task",
        8192,
        NULL,
        1,
        &s_ota_task_handle,
        1);
}
