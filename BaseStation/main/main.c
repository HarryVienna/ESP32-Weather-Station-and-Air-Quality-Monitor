/*******************************************************************************
 * LVGL on ESP32-P4-Module-DEV-KIT-C with 10.1" DSI Display
 *******************************************************************************
 *
 * Demonstrates:
 *   - Display initialization (MIPI DSI, JD9365, landscape mode)
 *   - GT911 capacitive touch input
 *   - LVGL 8
 *   - Sensor-Empfang via I2C vom ESP32-S3 Slave
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
#include "task/brightness_task.h"
#include "task/sensor_sen66_task.h"

static const char *TAG = "main";


void set_brightness(int32_t value) {
    display_set_brightness(value);

    char str[10];
    sprintf(str, "%lu", value);
    lv_label_set_text(objects.label_brightness, str);
}

void action_slider_set_backlight_brightness(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    set_brightness(value);
}


void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "      ESP32-P4 + 10.1\" DSI Display         ");
    ESP_LOGI(TAG, "============================================");

    ESP_ERROR_CHECK(display_init());

    lvgl_port_lock(0);
    ui_init();
    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_ERROR_CHECK(receiver_init());

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " Initialization complete!");
    ESP_LOGI(TAG, "============================================");

    receiver_start();

    xTaskCreatePinnedToCore(
      brightness_task,
      "Brightness Task",
      4096,
      NULL,
      1,
      NULL,
      1);

    xTaskCreatePinnedToCore(
      sensor_sen66_task,
      "SEN66 Task",
      4096,
      NULL,
      1,
      NULL,
      1);
}
