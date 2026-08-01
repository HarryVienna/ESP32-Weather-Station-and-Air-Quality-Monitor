#include "gui_ota.h"

#include "esp_lvgl_port.h"

#include "ota/ota_task.h"
#include "ui/ui.h"

/* The progress message box lives directly on the Weatherstation screen, so
 * no loadScreen() call is needed here. */

void gui_ota_update_available(const char *version)
{
    lvgl_port_lock(0);
    lv_label_set_text(objects.update_version, version);
    lv_obj_remove_flag(objects.message_box_update, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

/* "Install" button in message_box_update (see screens.c), wired by EEZ to
 * LV_EVENT_PRESSED. Runs on the LVGL task - only disables the button
 * (prevents a double click / second install task, but keeps it visible,
 * the progress message box stays open) and kicks off the download on its
 * own task, so this itself does not block. */
void action_event_weatherstation_update_pressed(lv_event_t *e)
{
    (void)e;
    lv_obj_add_state(objects.button_update, LV_STATE_DISABLED);
    ota_task_install_available_update();
}

void gui_ota_update_started(void)
{
    lvgl_port_lock(0);
    lv_bar_set_value(objects.progress_bar_update, 0, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void gui_ota_update_progress(int32_t percent)
{
    lvgl_port_lock(0);
    lv_bar_set_value(objects.progress_bar_update, percent, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void gui_ota_update_failed(void)
{
    lvgl_port_lock(0);
    lv_obj_add_flag(objects.message_box_update, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(objects.button_update, LV_STATE_DISABLED); /* re-enable for the next attempt */
    lvgl_port_unlock();
}
