#include <string.h>

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gui/gui.h"

#include "config/config.h"

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

static int s_retry_num = 0;
static bool s_retry_forever = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        disp_wifi_status(false, 0);

        if (s_retry_forever || s_retry_num < 10) {
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
bool wifi_connect(const char* ssid, const char* password, bool retry_forever) {

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

    s_retry_forever = retry_forever;
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
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

