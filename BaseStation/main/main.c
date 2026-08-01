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
#include "esp_ota_ops.h"
#include "lvgl.h"

#include "i18n/i18n.h"
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

    // Hardware Init
    ESP_ERROR_CHECK(display_init());

    ESP_ERROR_CHECK(wifi_init());

    ESP_ERROR_CHECK(i2c_manager_init());

    ESP_ERROR_CHECK(receiver_init());

    // GUI Init
    lvgl_port_lock(0);
    i18n_init(); // before ui_init(): screens already call _() while building up
    ui_init();
    i18n_apply_keyboard_layout(); // after ui_init(): needs objects.keyboard_text
    gui_weather_init_charts();
    gui_sen66_init_charts();
    gui_radiation_init_chart();
    lvgl_port_unlock();

    // Confirms an app installed via OTA as working, otherwise the
    // bootloader rolls back to the previous ota_0/ota_1 partition on the
    // next boot (see CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
    esp_ota_mark_app_valid_cancel_rollback();

    // Only for Screenshot Task
    //start_screenshot(10, 60);

    ESP_LOGI(TAG, "===================================================");
    ESP_LOGI(TAG, "           Initialization complete!                ");
    ESP_LOGI(TAG, "===================================================");

}
