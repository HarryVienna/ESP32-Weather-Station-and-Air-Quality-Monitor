#include "ui/ui.h"
#include "ui/actions.h"

/* Shows/hides the matching on-screen keyboard and attaches it to the
 * focused text field - one action_event_text_area_*() function per text
 * field, because EEZ Studio wires up a separate event per widget. Pure
 * LVGL mechanics, unrelated to WiFi/weather/sensors. */

void action_event_text_area_password_focus(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_text, objects.text_area_password);

      lv_obj_set_x(objects.keyboard_text, -29);
      lv_obj_set_y(objects.keyboard_text, -334);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_app_id_focus(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_text, objects.text_area_app_id);

      lv_obj_set_x(objects.keyboard_text, 183);
      lv_obj_set_y(objects.keyboard_text, -257);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_text, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_latitude_focus(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_latitude);

      lv_obj_set_x(objects.keyboard_numeric, 184);
      lv_obj_set_y(objects.keyboard_numeric, 296);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_longitude_focus(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_longitude);

      lv_obj_set_x(objects.keyboard_numeric, 655);
      lv_obj_set_y(objects.keyboard_numeric, 296);
    }
    if(event_code == LV_EVENT_DEFOCUSED) {
      lv_obj_add_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_event_text_area_hoehe_focus(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_FOCUSED) {

      lv_obj_clear_flag(objects.keyboard_numeric, LV_OBJ_FLAG_HIDDEN);
      lv_keyboard_set_textarea(objects.keyboard_numeric, objects.text_area_hoehe);

      lv_obj_set_x(objects.keyboard_numeric, 920);
      lv_obj_set_y(objects.keyboard_numeric, 296);
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
