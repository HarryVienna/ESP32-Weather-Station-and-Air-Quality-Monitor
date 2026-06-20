#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SETUP_SCREEN = 1,
    SCREEN_ID_WEATHERSTATION_SCREEN = 2,
    _SCREEN_ID_LAST = 2
};

typedef struct _objects_t {
    lv_obj_t *setup_screen;
    lv_obj_t *weatherstation_screen;
    lv_obj_t *panel_setup;
    lv_obj_t *panel_networks;
    lv_obj_t *dropdown_networks;
    lv_obj_t *button_scan;
    lv_obj_t *label_scan;
    lv_obj_t *panel_password;
    lv_obj_t *text_area_password;
    lv_obj_t *button_connect;
    lv_obj_t *label_scan_1;
    lv_obj_t *panel_app_id;
    lv_obj_t *text_area_app_id;
    lv_obj_t *panel_location;
    lv_obj_t *text_area_latitude;
    lv_obj_t *text_area_longitude;
    lv_obj_t *text_area_hoehe;
    lv_obj_t *panel_timezone;
    lv_obj_t *dropdown_region;
    lv_obj_t *dropdown_city;
    lv_obj_t *panel_base;
    lv_obj_t *text_area_basis;
    lv_obj_t *panel_sensors;
    lv_obj_t *text_area_sensor_name1;
    lv_obj_t *text_area_sensor_name2;
    lv_obj_t *text_area_sensor_name3;
    lv_obj_t *panel_start;
    lv_obj_t *button_starten;
    lv_obj_t *label_scan_2;
    lv_obj_t *keyboard_text;
    lv_obj_t *keyboard_numeric;
    lv_obj_t *current;
    lv_obj_t *obj0;
    lv_obj_t *date_time;
    lv_obj_t *wifi;
    lv_obj_t *weather_icon;
    lv_obj_t *temp_current;
    lv_obj_t *temp_current_unit;
    lv_obj_t *clouds_current;
    lv_obj_t *uv_current;
    lv_obj_t *wind_direction_current_icon;
    lv_obj_t *wind_speed_current;
    lv_obj_t *wind_gust_current;
    lv_obj_t *sunrise_current;
    lv_obj_t *sunset_current;
    lv_obj_t *obj1;
    lv_obj_t *obj1__base_4;
    lv_obj_t *obj1__obj23;
    lv_obj_t *obj1__name_base_4;
    lv_obj_t *obj1__temp_base_4;
    lv_obj_t *obj1__obj24;
    lv_obj_t *obj1__humidity_base_4;
    lv_obj_t *obj1__voc_13;
    lv_obj_t *obj1__obj25;
    lv_obj_t *obj1__voc_14;
    lv_obj_t *obj1__obj26;
    lv_obj_t *obj1__voc_15;
    lv_obj_t *obj1__obj27;
    lv_obj_t *obj1__voc_16;
    lv_obj_t *obj1__voc_17;
    lv_obj_t *obj1__voc_18;
    lv_obj_t *obj1__voc_19;
    lv_obj_t *obj1__obj28;
    lv_obj_t *obj2;
    lv_obj_t *obj2__sensor0_3;
    lv_obj_t *obj2__obj2;
    lv_obj_t *obj2__name0_3;
    lv_obj_t *obj2__temp0_2;
    lv_obj_t *obj2__obj3;
    lv_obj_t *obj2__hunidity0_2;
    lv_obj_t *obj2__line_status0_1;
    lv_obj_t *obj2__volt0_1;
    lv_obj_t *obj2__update0_1;
    lv_obj_t *obj3;
    lv_obj_t *obj3__sensor0_3;
    lv_obj_t *obj3__obj2;
    lv_obj_t *obj3__name0_3;
    lv_obj_t *obj3__temp0_2;
    lv_obj_t *obj3__obj3;
    lv_obj_t *obj3__hunidity0_2;
    lv_obj_t *obj3__line_status0_1;
    lv_obj_t *obj3__volt0_1;
    lv_obj_t *obj3__update0_1;
    lv_obj_t *obj4;
    lv_obj_t *obj4__sensor0_3;
    lv_obj_t *obj4__obj2;
    lv_obj_t *obj4__name0_3;
    lv_obj_t *obj4__temp0_2;
    lv_obj_t *obj4__obj3;
    lv_obj_t *obj4__hunidity0_2;
    lv_obj_t *obj4__line_status0_1;
    lv_obj_t *obj4__volt0_1;
    lv_obj_t *obj4__update0_1;
    lv_obj_t *obj5;
    lv_obj_t *obj5__sensor0_3;
    lv_obj_t *obj5__obj2;
    lv_obj_t *obj5__name0_3;
    lv_obj_t *obj5__temp0_2;
    lv_obj_t *obj5__obj3;
    lv_obj_t *obj5__hunidity0_2;
    lv_obj_t *obj5__line_status0_1;
    lv_obj_t *obj5__volt0_1;
    lv_obj_t *obj5__update0_1;
    lv_obj_t *obj6;
    lv_obj_t *obj6__sensor0_3;
    lv_obj_t *obj6__obj2;
    lv_obj_t *obj6__name0_3;
    lv_obj_t *obj6__temp0_2;
    lv_obj_t *obj6__obj3;
    lv_obj_t *obj6__hunidity0_2;
    lv_obj_t *obj6__line_status0_1;
    lv_obj_t *obj6__volt0_1;
    lv_obj_t *obj6__update0_1;
    lv_obj_t *obj7;
    lv_obj_t *obj7__sensor0_3;
    lv_obj_t *obj7__obj2;
    lv_obj_t *obj7__name0_3;
    lv_obj_t *obj7__temp0_2;
    lv_obj_t *obj7__obj3;
    lv_obj_t *obj7__hunidity0_2;
    lv_obj_t *obj7__line_status0_1;
    lv_obj_t *obj7__volt0_1;
    lv_obj_t *obj7__update0_1;
    lv_obj_t *hourly;
    lv_obj_t *hourly_chart;
    lv_obj_t *daily;
    lv_obj_t *daily_chart;
} objects_t;

extern objects_t objects;

void create_screen_setup_screen();
void tick_screen_setup_screen();

void create_screen_weatherstation_screen();
void tick_screen_weatherstation_screen();

void create_user_widget_sensor_bme280(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_bme280(int startWidgetIndex);

void create_user_widget_sensor_bme280_2(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_bme280_2(int startWidgetIndex);

void create_user_widget_base(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base(int startWidgetIndex);

void create_user_widget_base_0(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_0(int startWidgetIndex);

void create_user_widget_base_1(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_1(int startWidgetIndex);

void create_user_widget_base_2(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_2(int startWidgetIndex);

void create_user_widget_base_3(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_3(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/