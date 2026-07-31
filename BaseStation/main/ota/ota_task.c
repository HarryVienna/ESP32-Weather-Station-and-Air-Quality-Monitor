#include "ota_task.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "weather/weather_http.h"
#include "gui/ota/gui_ota.h"

static const char *TAG = "ota_task";

/* Zwischen dem Fund eines Updates (check_for_update(), OTA-Task) und dem
 * tatsaechlichen Installieren (Button-Klick auf dem LVGL-Task, siehe
 * ota_task_install_available_update()) liegt eine Nutzerbestaetigung -
 * download_url muss also bis dahin irgendwo warten. Nur ein Update kann
 * gleichzeitig anstehen. */
static char s_pending_download_url[512];

/* Haelt den OTA-Task an, statt periodisch ins Leere zu pollen, waehrend auf
 * die Nutzerbestaetigung gewartet wird (siehe check_for_update()) - wird von
 * install_task() im Fehlerfall wieder aufgeweckt (bei Erfolg rebootet das
 * Geraet eh, siehe apply_update()). */
static TaskHandle_t s_ota_task_handle = NULL;

/* Repo, an das der Release-Workflow (.github/workflows/release.yml) das
 * Firmware-Binary als Release-Asset anhaengt. */
#define GITHUB_OWNER          "HarryVienna"
#define GITHUB_REPO           "ESP32-Weather-Station-and-Air-Quality-Monitor"
#define GITHUB_RELEASE_URL    "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest"
#define OTA_ASSET_NAME        "Basestation.bin"

/* Erster Check kurz nach Task-Start, danach alle 24h - GitHub-Releases sind
 * kein Vorgang, der haeufigeres Pollen rechtfertigt. */
#define OTA_FIRST_CHECK_DELAY_MS   (1000 * 30)
#define OTA_CHECK_INTERVAL_MS      (1000 * 60 * 60 * 1)

/* Sucht im "assets"-Array der GitHub-Release-JSON den Eintrag mit
 * name == OTA_ASSET_NAME und liefert dessen browser_download_url (Eigentum
 * bleibt beim uebergebenen cJSON-Baum, nicht selbst freigeben). */
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

/* Laedt das Firmware-Binary von download_url in die inaktive OTA-Partition
 * und startet bei Erfolg neu. Kehrt nur bei Fehler zurueck (und wechselt dann
 * zurueck auf den Weatherstation-Screen, statt auf "Upgrading..." haengen zu
 * bleiben).
 *
 * Nutzt bewusst die granulare esp_https_ota_begin()/_perform()/_finish()-API
 * statt der einfachen esp_https_ota() - nur so kommt man zwischendurch an
 * esp_https_ota_get_image_len_read()/_get_image_size() fuer den Fortschritts-
 * balken (siehe gui/ota/gui_ota.c, kein direkter LVGL-Zugriff hier). */
static void apply_update(const char *download_url)
{
    ESP_LOGI(TAG, "Updating firmware from %s", download_url);

    gui_ota_update_started();

    esp_http_client_config_t http_config = {
        .url             = download_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* Auto-Redirect bewusst NICHT deaktiviert: GitHub-Asset-Downloads
         * leiten auf objects.githubusercontent.com um, mit einer langen
         * signierten Query-String (AWS-artige Signatur-Parameter). Der
         * 512-Byte-Default-Puffer (DEFAULT_HTTP_BUF_SIZE) reicht dafuer nicht
         * - weder zum Lesen der Redirect-Header (buffer_size) noch zum
         * Aufbauen der ausgehenden Request-Zeile fuer die umgeleitete
         * Anfrage, die genau diese lange Query-String enthaelt
         * (buffer_size_tx, siehe http_client_prepare_first_line() in
         * esp_http_client.c - das war die eigentliche Quelle von
         * "HTTP_CLIENT: Out of buffer"). Beide daher vergroessert. */
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
    ESP_LOGI(TAG, "OTA update successful, rebooting");
    esp_restart();

fail:
    ESP_LOGE(TAG, "OTA update failed");
    gui_ota_update_failed();
}

/* Eigener Task fuer apply_update(): der Aufrufer (ota_task_install_available_
 * update()) laeuft auf dem LVGL-Task (Button-Klick), apply_update() ist aber
 * ein minutenlanger blockierender Download - direkt aufgerufen wuerde das
 * die gesamte UI (Rendering, Touch) fuer die Dauer des Downloads einfrieren. */
static void install_task(void *pvParameter)
{
    apply_update(s_pending_download_url);
    /* apply_update() kehrt nur im Fehlerfall zurueck (Erfolg -> esp_restart()) -
     * OTA-Task wieder aufwecken, damit er die periodische Pruefung fortsetzt. */
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
    bool has_suffix; /* "-dirty" und/oder "-<N>-g<hash>" hinter X.Y.Z */
} app_version_t;

/* Parst "[v]X.Y.Z[-beliebiger Rest]" (das Format, das "git describe --always
 * --tags --dirty" erzeugt, siehe project.cmake - "-dirty" bei uncommitteten
 * Aenderungen, "-<N>-g<hash>" bei Commits nach dem letzten Tag). Liefert
 * false, wenn schon X.Y.Z nicht gefunden wird (z.B. Fallback auf einen
 * nackten Commit-Hash ohne erreichbaren Tag) - dann ist kein sinnvoller
 * Vergleich moeglich. */
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

/* Vergleicht wie Semver (Major/Minor/Patch entscheiden zuerst), mit einer
 * Ausnahme bei sonst gleicher Version: eine Version MIT Suffix ("-dirty"
 * bzw. "-<N>-g<hash>") zaehlt als NEUER als die nackte X.Y.Z, auf der sie
 * basiert - git-historisch liegt sie ja tatsaechlich danach (mehr Commits
 * und/oder lokale Aenderungen obendrauf). Also z.B. 0.1.0 < 0.1.0-dirty <
 * 0.2.0. Gibt <0 zurueck wenn a<b, 0 wenn gleich, >0 wenn a>b. */
static int compare_versions(const app_version_t *a, const app_version_t *b)
{
    //return 1; // DEBUG
    if (a->major != b->major) return a->major - b->major;
    if (a->minor != b->minor) return a->minor - b->minor;
    if (a->patch != b->patch) return a->patch - b->patch;
    return (int)a->has_suffix - (int)b->has_suffix;
}

/* Ein Check-Zyklus: neuestes GitHub-Release abfragen, Version vergleichen,
 * bei Unterschied aktualisieren. */
static void check_for_update(esp_http_client_handle_t client, weather_http_response_t *response)
{
    const char *running_version = esp_app_get_description()->version;

    esp_http_client_set_header(client, "User-Agent", "BaseStation-OTA");

    cJSON *json = weather_http_get_json(client, response, GITHUB_RELEASE_URL);
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

    /* download_url und tag->valuestring zeigen in json hinein, also erst
     * nach dem letzten Zugriff auf json freigeben - gui_ota_update_available()
     * kopiert sich die Versionsnummer selbst (lv_label_set_text()), muss also
     * ebenfalls noch davor aufgerufen werden. */
    strncpy(s_pending_download_url, download_url, sizeof(s_pending_download_url) - 1);
    s_pending_download_url[sizeof(s_pending_download_url) - 1] = '\0';
    ESP_LOGI(TAG, "Update available (%s) - waiting for user confirmation", tag->valuestring);
    gui_ota_update_available(tag->valuestring);
    cJSON_Delete(json);

    /* Haengt hier, bis install_task() (Fehlerfall) oder ein Reboot (Erfolg)
     * uns wieder aufweckt - kein Grund, in der Zwischenzeit periodisch
     * aufzuwachen und erneut zu pruefen. */
    vTaskSuspend(NULL);
}

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Start OTA task");

    weather_http_response_t response = {0};
    esp_http_client_handle_t client = weather_http_client_create(&response);

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
