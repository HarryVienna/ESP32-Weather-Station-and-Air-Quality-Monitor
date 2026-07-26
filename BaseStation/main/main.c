/*******************************************************************************
 * ESP32 Weather Station & Air Quality Monitor
 *******************************************************************************
 *
 * Hardware:
 *   Board:   Waveshare ESP32-P4-Module-DEV-KIT
 *   Display: Waveshare 10.1-DSI-TOUCH (800x1280, landscape 1280x800)
 *
 ******************************************************************************/

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "i2c/i2c_manager.h"
#include "display/display.h"
#include "receiver/receiver.h"
#include "ui/ui.h"
#include "gui/weather/gui_weather.h"
#include "gui/sensors/gui_sensors.h"
#include "gui/sensors/gui_sen66.h"
#include "wifi/network.h"
#include "lvgl/lv_screenshot.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "===================================================");
    ESP_LOGI(TAG, "      ESP32 Wetterstation & Raumluft-Monitor       ");
    ESP_LOGI(TAG, "===================================================");

    ESP_ERROR_CHECK(display_init());

    wifi_init();

    lvgl_port_lock(0);
    ui_init();
    gui_weather_init_charts();
    gui_sen66_init_charts();
    gui_radiation_init_chart();
    lvgl_port_unlock();

    ESP_ERROR_CHECK(i2c_manager_init());

    ESP_ERROR_CHECK(receiver_init());


    // Only for Screenshot Task
    // if (wifi_connect("xxx", "xxx")) {
    //     wifi_stay_connected_forever();
    //     wifi_sync_time();
    //     start_screenshot(10, 60);
    // } else {
    //     ESP_LOGE(TAG, "WLAN-Verbindung fehlgeschlagen, Screenshot-Task wird nicht gestartet");
    // }

    ESP_LOGI(TAG, "===================================================");
    ESP_LOGI(TAG, "           Initialization complete!                ");
    ESP_LOGI(TAG, "===================================================");

}
