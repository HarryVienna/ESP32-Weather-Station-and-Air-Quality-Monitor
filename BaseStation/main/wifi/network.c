#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gui/status/gui_status.h"

#include "config/config.h"
#include "network.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define MAC_STR_LEN 18

static const char* TAG = "WIFI";

typedef struct struct_data {
    uint8_t msg_type;
    uint8_t sensor_nr;
    uint32_t voltage;
    double pressure;
    double temperature;
    double humidity;
} struct_data;




/**
 * @brief     Print MAC address to Serial monitor
 *
 * @param     mac_addr  Pointer to the MAC address array
 *
 * @details   Formats the MAC address provided as an array of uint8_t into a string.
 *            Prints the formatted MAC address to the Serial monitor.
 */
char* get_mac_string(const uint8_t *mac_addr, char *macStrBuffer) {
    snprintf(macStrBuffer, MAC_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return macStrBuffer;
}


/**
 * @brief Initialize WIFI
 *
 * This function initializes the WIFI driver.
 */
void wifi_init(void) {
    ESP_LOGI(TAG, "Install WIFI driver");

    esp_err_t ret;

    //Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      if (nvs_flash_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nvs flash");
      }
    }

    // Initialize the underlying TCP/IP stack
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init netif");  
    }

    // Create default event loop
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set create event loop");      
    }

    // Creates default WIFI ST
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // Set hostname
    if (esp_netif_set_hostname(sta_netif, HOST_NAME)) {
        ESP_LOGE(TAG, "Failed to set hostname");
    }

    //  Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi");   
    }
    
    // Set the WiFi operating mode
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mode");   
    }

    // Set storage to RAM
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set storage");
    }


}


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECT_MAX_RETRIES 10

static int s_retry_num = 0;

/* Persistente Hintergrund-Policy, unabhaengig vom aktuellen Verbindungs-
 * versuch - siehe wifi_stay_connected_forever() in network.h. Wird von
 * wifi_connect() NICHT zurueckgesetzt, bleibt also ueber mehrere Aufrufe
 * hinweg bestehen, sobald sie einmal gesetzt wurde. */
static bool s_stay_connected_forever = false;

/* Menschenlesbare Kurzbeschreibung der gaengigsten WIFI_REASON_*-Codes (siehe
 * esp_wifi_types_generic.h) - hilft beim Einordnen, ob ein langsamer/
 * fehlschlagender Connect am Passwort, am Router oder an schlechtem Empfang
 * liegt, statt nur die nackte Zahl zu sehen. */
static const char* wifi_disconnect_reason_str(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "unspecified";
        case WIFI_REASON_AUTH_EXPIRE: return "auth expired";
        case WIFI_REASON_AUTH_LEAVE: return "auth leave";
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY: return "disassoc (inactivity)";
        case WIFI_REASON_ASSOC_LEAVE: return "assoc leave (STA disconnected)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout (falsches Passwort?)";
        case WIFI_REASON_MIC_FAILURE: return "MIC failure";
        case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout (schlechter Empfang)";
        case WIFI_REASON_NO_AP_FOUND: return "AP nicht gefunden";
        case WIFI_REASON_AUTH_FAIL: return "Authentifizierung fehlgeschlagen (falsches Passwort?)";
        case WIFI_REASON_ASSOC_FAIL: return "Assoziation fehlgeschlagen";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Handshake-Timeout";
        case WIFI_REASON_CONNECTION_FAIL: return "Verbindung fehlgeschlagen";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "kein AP mit passender Sicherheit gefunden";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "AP-Signal zu schwach (RSSI-Schwelle)";
        default: return "unbekannt";
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected: reason=%d (%s), rssi=%d",
                 disconnected->reason, wifi_disconnect_reason_str(disconnected->reason), disconnected->rssi);

        disp_wifi_status(false, 0);

        if (s_stay_connected_forever || s_retry_num < WIFI_CONNECT_MAX_RETRIES) {
            // Kurze Pause vor dem Retry: manche Router/APs (Band-Steering, Mesh)
            // haben den alten Assoziations-Eintrag fuer diese Station noch nicht
            // abgeraeumt, wenn sofort erneut connect() aufgerufen wird - das
            // aeussert sich als auth-expired/4-way-handshake-timeout trotz gutem
            // RSSI (siehe wifi_disconnect_reason_str() oben).
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap_info;
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
        disp_wifi_status(true, rssi);

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } 
}

/**
 * @brief Connect WIFI
 *
 * This function connects to WIFI
 */
bool wifi_connect(const char* ssid, const char* password) {

    ESP_LOGI(TAG, "connecting to ap SSID: >%s<  password: >%s<", ssid, password);

    ESP_ERROR_CHECK(esp_wifi_stop());

    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above). Immer bounded durch
     * WIFI_CONNECT_MAX_RETRIES (s_stay_connected_forever betrifft nur spaetere, hier nicht abgewartete Events -
     * siehe event_handler()), deshalb ist portMAX_DELAY hier sicher: WIFI_FAIL_BIT wird garantiert irgendwann
     * gesetzt, falls nicht vorher WIFI_CONNECTED_BIT kommt. */
    ESP_LOGI(TAG, "Waiting for xEventGroupWaitBits");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    return (bits & WIFI_CONNECTED_BIT);

}

void wifi_stay_connected_forever(void)
{
    s_stay_connected_forever = true;
}


/* ============================================================================
 * NTP-Zeitsynchronisation (nach erfolgreichem WLAN-Connect)
 * ============================================================================ */

static SemaphoreHandle_t sync_semaphore;
static bool sntp_started = false;

/**
 * @brief Callback function from time sync
 */
static void sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "Syncing date/time: %s", ctime(&tv->tv_sec));
    xSemaphoreGive(sync_semaphore);
}

void wifi_sync_time(void)
{
    if (sntp_started) {
        // esp_sntp_init() darf nur einmal aufgerufen werden - bei mehrfachem
        // Klick auf "Verbinden" (z.B. erst falsches, dann richtiges Passwort)
        // ist die Zeit vom ersten erfolgreichen Aufruf bereits synchronisiert.
        return;
    }
    sntp_started = true;

    sync_semaphore = xSemaphoreCreateBinary();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(&sync_callback);
    esp_sntp_init();

    // Wait for the sync_callback to give the semaphore (bounded: an unreachable
    // NTP server must not block the caller forever)
    if (xSemaphoreTake(sync_semaphore, pdMS_TO_TICKS(10000)) == pdTRUE) {
        ESP_LOGI(TAG, "Time synchronization successful");
    } else {
        ESP_LOGW(TAG, "Time synchronization timeout");
    }
}


/* ============================================================================
 * WLAN-Scan (Setup-Screen-Dropdown)
 * ============================================================================ */

/**
 * @brief     Task for scanning nearby Wi-Fi networks
 *
 * @param     pvParameter   wifiscan_done_cb_t, called with the result (may be NULL)
 *
 * @details   Initiates Wi-Fi scanning to discover nearby networks and their signal strengths.
 *            Prints the scanned networks and their information, then hands the
 *            newline-separated list to the completion callback.
 *            Deletes the task once the scan is complete.
 */
static void wifiscan_task(void *pvParameter) {
    wifiscan_done_cb_t on_done = (wifiscan_done_cb_t)pvParameter;

    ESP_LOGI(TAG, "Start wifiscan_task");

    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi");
    }

    ESP_ERROR_CHECK(esp_wifi_disconnect());
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 1000,
        .scan_time.active.max = 5000,
    };

    uint16_t ap_count = 16;
    esp_wifi_scan_start(&scan_config, true);
    esp_wifi_scan_get_ap_records(&ap_count, NULL);

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    char allNetworks[4096] = {0}; // Assuming a maximum of 4096 characters for all network names

    // Print scanned networks
    for (uint16_t i = 0; i < ap_count; i++) {
        // Check if the SSID is not empty
        if (strlen((const char *)ap_records[i].ssid) > 0) {
             char item[128]; // Assuming a maximum of 128 characters per network item
            snprintf(item, sizeof(item), "%s (%d) %s", (const char *)ap_records[i].ssid, ap_records[i].rssi, (ap_records[i].authmode == WIFI_AUTH_OPEN) ? "" : "*");
            ESP_LOGI(TAG, "%s", item);

            strlcat(allNetworks, (const char *)ap_records[i].ssid, sizeof(allNetworks));
            if (i != ap_count - 1) {
                strlcat(allNetworks, "\n", sizeof(allNetworks)); // Add newline character except for the last SSID
            }
        }

    }

    free(ap_records);

    ESP_ERROR_CHECK(esp_wifi_stop());

    if (on_done) {
        on_done(allNetworks);
    }

    vTaskDelete(NULL); // Delete the task when done
}

void wifiscan_start(wifiscan_done_cb_t on_done) {
    xTaskCreatePinnedToCore(
        wifiscan_task,    // Task function
        "WiFiScan Task",  // Task name
        16000,            // Stack size (bytes)
        (void *)on_done,  // Task input parameter
        16,               // Task priority
        NULL,             // Task handle
        0                 // Core to run the task on (0 or 1)
    );
}


/* ============================================================================
 * WLAN-Verbindungstest (Setup-Screen-"Verbinden"-Button)
 * ============================================================================ */

typedef struct {
  char* ssid;
  char* password;
  wificonnect_done_cb_t on_done;
} local_wifi_sta_config_t;

/**
 * @brief     Task for connecting to a Wi-Fi network
 *
 * @param     pvParameter   Pointer to a malloc'd local_wifi_sta_config_t (this
 *                           task takes ownership and frees it, including its
 *                           ssid/password strings)
 *
 * @details   Attempts to connect to the specified Wi-Fi network using provided credentials,
 *            then hands the result to the completion callback.
 *            Deletes the task once the connection attempt is finished.
 */
static void wificonnect_task(void *pvParameter) {

    ESP_LOGI(TAG, "Start wificonnect_task");

    local_wifi_sta_config_t *wifiParams = (local_wifi_sta_config_t *)pvParameter;

    bool connected = wifi_connect(wifiParams->ssid, wifiParams->password);

    if (wifiParams->on_done) {
        wifiParams->on_done(connected);
    }

    free(wifiParams->ssid);
    free(wifiParams->password);
    free(wifiParams);

    vTaskDelete(NULL); // Delete the task when done
}

void wificonnect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done) {
    local_wifi_sta_config_t *wifiParams = (local_wifi_sta_config_t *)malloc(sizeof(local_wifi_sta_config_t));
    wifiParams->ssid = strdup(ssid);
    wifiParams->password = strdup(password);
    wifiParams->on_done = on_done;

    xTaskCreatePinnedToCore(
        wificonnect_task,   // Task function
        "WiFiConnect Task", // Task name
        4096,               // Stack size (bytes)
        wifiParams,         // Task input parameter
        16,                 // Task priority
        NULL,               // Task handle
        0                   // Core to run the task on (0 or 1)
    );
}


