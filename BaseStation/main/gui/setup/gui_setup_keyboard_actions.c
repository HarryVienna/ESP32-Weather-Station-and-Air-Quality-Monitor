#include "ui/ui.h"
#include "ui/actions.h"

/* Blendet die passende Bildschirmtastatur ein/aus und haengt sie an das
 * fokussierte Textfeld - eine action_event_text_area_*() Funktion pro
 * Textfeld, weil EEZ Studio pro Widget ein eigenes Event verdrahtet. Reine
 * LVGL-Mechanik, kein Bezug zu WLAN/Wetter/Sensoren. */

void action_event_text_area_password(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_text, objects.text_area_password);

      lv_obj_set_x(objects.keyboard_text, -38);
      lv_obj_set_y(objects.keyboard_text, -155);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_app_id(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_text, objects.text_area_app_id);

      lv_obj_set_x(objects.keyboard_text, -38);
      lv_obj_set_y(objects.keyboard_text, -87);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_latitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_latitude);

      lv_obj_set_x(objects.keyboard_numeric, 99);
      lv_obj_set_y(objects.keyboard_numeric, 277);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_longitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_longitude);

      lv_obj_set_x(objects.keyboard_numeric, 478);
      lv_obj_set_y(objects.keyboard_numeric, 277);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_hoehe(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_hoehe);

      lv_obj_set_x(objects.keyboard_numeric, 691);
      lv_obj_set_y(objects.keyboard_numeric, 277);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_keyboard_text(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CANCEL) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
    if(event_code == LV_EVENT_READY) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_keyboard_numeric(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CANCEL) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
    if(event_code == LV_EVENT_READY) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}
