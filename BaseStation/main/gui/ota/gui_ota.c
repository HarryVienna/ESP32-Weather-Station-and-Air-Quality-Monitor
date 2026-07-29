#include "gui_ota.h"

#include "esp_lvgl_port.h"

#include "ota/ota_task.h"
#include "ui/ui.h"

/* Die MessageBox mit Fortschrittsbalken lebt direkt auf dem Weatherstation-
 * Screen, es wird also nirgends mehr loadScreen() gebraucht. */

void gui_ota_update_available(void)
{
    lvgl_port_lock(0);
    lv_obj_remove_flag(objects.message_box_update, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

/* "Installieren"-Button in message_box_update (siehe screens.c), von EEZ auf
 * LV_EVENT_PRESSED verdrahtet. Laeuft auf dem LVGL-Task - deaktiviert nur den
 * Button (verhindert Doppelklick/zweiten Install-Task, bleibt aber sichtbar,
 * die MessageBox mit dem Fortschrittsbalken bleibt offen) und stoesst den
 * Download auf einem eigenen Task an, blockiert also selbst nicht. */
void action_event_message_box_update(lv_event_t *e)
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
    lv_obj_remove_state(objects.button_update, LV_STATE_DISABLED); /* fuer den naechsten Versuch wieder aktivieren */
    lvgl_port_unlock();
}
