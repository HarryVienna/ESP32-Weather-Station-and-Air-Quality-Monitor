#include "gui_ota.h"

#include "esp_lvgl_port.h"

#include "ui/ui.h"

void gui_ota_update_started(void)
{
    lvgl_port_lock(0);
    loadScreen(SCREEN_ID_UPDATE_SCREEN);
    lv_bar_set_value(objects.progress_bar, 0, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void gui_ota_update_progress(int32_t percent)
{
    lvgl_port_lock(0);
    lv_bar_set_value(objects.progress_bar, percent, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void gui_ota_update_failed(void)
{
    lvgl_port_lock(0);
    loadScreen(SCREEN_ID_WEATHERSTATION_SCREEN);
    lvgl_port_unlock();
}
