#include "gui_status.h"

#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display/display.h"
#include "config/config.h"
#include "ui/ui.h"
#include "../sensors/gui_sensors.h"
#include "clock_task.h"
#include "brightness_task.h"

/**
 * @brief  Faerbt das WLAN-Icon im Weatherstation-Screen nach Verbindungsstatus/RSSI.
 *
 * @param  status    true = verbunden, false = getrennt
 * @param  rssi_dbm  Empfangsfeldstaerke in dBm (nur relevant wenn status==true)
 */
void disp_wifi_status(bool status, int8_t rssi_dbm)
{
  lv_color_t color = status ? level_color_desc((float)rssi_dbm, &THRESH_RSSI_DBM)
                             : lv_color_hex(COLOR_RED);

  lvgl_port_lock(0);
  lv_obj_set_style_img_recolor(objects.current__wifi, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(objects.current__wifi, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lvgl_port_unlock();
}

void disp_date_time(char *date_time)
{
  lvgl_port_lock(0);
  lv_label_set_text(objects.current__date_time, date_time);
  lvgl_port_unlock();
}

void set_brightness(uint16_t brightness)
{
  display_set_brightness(brightness);
}

void gui_status_start_clock_task(void)
{
  xTaskCreatePinnedToCore(
      clock_task,
      "Clock Task",
      4096,
      NULL,
      1,
      NULL,
      1);
}

void gui_status_start_brightness_task(void)
{
  xTaskCreatePinnedToCore(
      brightness_task,
      "Brightness Task",
      4096,
      NULL,
      1,
      NULL,
      1);
}
