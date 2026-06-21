#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wificonnect_task.h"

#include "wifi/network.h"


static const char* TAG = "wificonnect_task";

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

    bool connected = wifi_connect(wifiParams->ssid, wifiParams->password, false);

    if (wifiParams->on_done) {
        wifiParams->on_done(connected);
    }

    free(wifiParams->ssid);
    free(wifiParams->password);
    free(wifiParams);

    vTaskDelete(NULL); // Delete the task when done
}

/**
 * @brief     Connect to a Wi-Fi network in its own task
 *
 * @param     ssid      network name (copied; caller retains ownership)
 * @param     password  network password (copied; caller retains ownership)
 * @param     on_done   called from the connect task with the connection result
 *                       once the attempt finishes (may be NULL)
 *
 * @details   Runs wificonnect_task() on a dedicated task so the (slow, blocking)
 *            connection attempt doesn't run on the caller's task. on_done runs
 *            on that task, not the caller's - lock around any UI access inside it.
 */
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