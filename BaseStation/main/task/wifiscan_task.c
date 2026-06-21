#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "wifiscan_task.h"


static const char* TAG = "wifiscan_task";

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

/**
 * @brief     Start a one-shot Wi-Fi scan in its own task
 *
 * @param     on_done   called from the scan task with the newline-separated
 *                       list of found SSIDs once the scan completes (may be NULL)
 *
 * @details   Runs wifiscan_task() on a dedicated task so the (slow, blocking)
 *            scan doesn't run on the caller's task. on_done runs on that task,
 *            not the caller's - lock around any UI access inside it.
 */
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