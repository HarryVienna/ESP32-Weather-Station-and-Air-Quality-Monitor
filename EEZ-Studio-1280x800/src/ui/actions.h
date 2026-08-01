#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_event_setup_screen_loaded(lv_event_t * e);
extern void action_event_setup_start_pressed(lv_event_t * e);
extern void action_event_wifi_scan_pressed(lv_event_t * e);
extern void action_event_wifi_connect_pressed(lv_event_t * e);
extern void action_event_restart_pressed(lv_event_t * e);
extern void action_event_text_area_app_id_focus(lv_event_t * e);
extern void action_event_text_area_password_focus(lv_event_t * e);
extern void action_event_text_area_latitude_focus(lv_event_t * e);
extern void action_event_text_area_longitude_focus(lv_event_t * e);
extern void action_event_text_area_hoehe_focus(lv_event_t * e);
extern void action_event_network_value_changed(lv_event_t * e);
extern void action_event_password_value_changed(lv_event_t * e);
extern void action_event_api_value_changed(lv_event_t * e);
extern void action_event_apikey_value_changed(lv_event_t * e);
extern void action_event_lat_value_changed(lv_event_t * e);
extern void action_event_long_value_changed(lv_event_t * e);
extern void action_event_alt_value_changed(lv_event_t * e);
extern void action_event_timezone_value_changed(lv_event_t * e);
extern void action_event_timezonecity_value_changed(lv_event_t * e);
extern void action_event_language_value_changed(lv_event_t * e);
extern void action_event_base_value_changed(lv_event_t * e);
extern void action_event_sensor0_value_changed(lv_event_t * e);
extern void action_event_sensor1_value_changed(lv_event_t * e);
extern void action_event_sensor2_value_changed(lv_event_t * e);
extern void action_event_sensor3_value_changed(lv_event_t * e);
extern void action_event_sensor4_value_changed(lv_event_t * e);
extern void action_event_sensor5_value_changed(lv_event_t * e);
extern void action_event_keyboard_text(lv_event_t * e);
extern void action_event_keyboard_numeric(lv_event_t * e);
extern void action_event_weatherstation_screen_loaded(lv_event_t * e);
extern void action_event_weatherstation_update_pressed(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/