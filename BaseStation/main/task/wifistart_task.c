#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"

#include "wifistart_task.h"
#include "wifi/network.h"
#include "nvs/preferences.h"
#include "config/config.h"

static const char* TAG = "wifistart_task";

static SemaphoreHandle_t sync_semaphore;

/**
 * @brief Callback function from time sync
 */
static void sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "Syncing date/time: %s", ctime(&tv->tv_sec));
    xSemaphoreGive(sync_semaphore);
}

/**
 * @brief     Task: start WiFi connection and setup time synchronization
 *
 * @param     pvParameter   Pointer to task parameters (not used in this function)
 *
 * @details   Initializes WiFi connection using stored credentials.
 *            Connects to the specified SSID using the provided password.
 *            Configures time synchronization based on the given timezone and NTP server.
 */
static void wifistart_task(void *pvParameter) {

    nvs_handle_t nvs_handle;
    nvs_open("weatherstation", NVS_READONLY, &nvs_handle);

    char* ssid = get_string_from_nvs(nvs_handle, "ssid", "");
    char* password = get_string_from_nvs(nvs_handle, "password", "");
    char* tz = get_string_from_nvs(nvs_handle, "tz", "");

    nvs_close(nvs_handle);

    wifi_connect(ssid, password, true);

    // Create and take the semaphore
    sync_semaphore = xSemaphoreCreateBinary();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(&sync_callback);
    esp_sntp_init();

    setenv("TZ", tz, 1);
    tzset();

    // Wait for the sync_callback to give the semaphore (bounded: an unreachable
    // NTP server must not block the caller forever)
    if (xSemaphoreTake(sync_semaphore, pdMS_TO_TICKS(10000)) == pdTRUE) {
        ESP_LOGI(TAG, "Time synchronization successful");
    } else {
        ESP_LOGW(TAG, "Time synchronization timeout");
    }

    vTaskDelete(NULL); // Delete the task when done
}

/**
 * @brief     Bring up Wi-Fi and NTP time sync in its own task
 *
 * @details   Runs wifistart_task() on a dedicated task so the (slow, blocking)
 *            Wi-Fi connect and NTP sync don't run on the caller's task.
 */
void wifistart_start(void) {
    xTaskCreatePinnedToCore(
        wifistart_task,    // Task function
        "WiFiStart Task",  // Task name
        4096,              // Stack size (bytes)
        NULL,              // Task input parameter
        1,                 // Task priority
        NULL,              // Task handle
        1                  // Core to run the task on (0 or 1)
    );
}
