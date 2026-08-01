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
    lv_obj_t *label_connect;
    lv_obj_t *panel_app_id;
    lv_obj_t *dropdown_api;
    lv_obj_t *text_area_app_id;
    lv_obj_t *panel_location;
    lv_obj_t *text_area_latitude;
    lv_obj_t *text_area_longitude;
    lv_obj_t *text_area_hoehe;
    lv_obj_t *panel_timezone;
    lv_obj_t *dropdown_region;
    lv_obj_t *dropdown_city;
    lv_obj_t *panel_language;
    lv_obj_t *dropdown_language;
    lv_obj_t *restart_description;
    lv_obj_t *button_restart;
    lv_obj_t *label_restart;
    lv_obj_t *panel_base;
    lv_obj_t *basis_icon;
    lv_obj_t *panel_sensors;
    lv_obj_t *sensor_0_name;
    lv_obj_t *sensor_2_name;
    lv_obj_t *sensor_4_name;
    lv_obj_t *panel_sensors_1;
    lv_obj_t *sensor_1_name;
    lv_obj_t *sensor_3_name;
    lv_obj_t *sensor_5_name;
    lv_obj_t *panel_start;
    lv_obj_t *button_starten;
    lv_obj_t *label_start;
    lv_obj_t *keyboard_text;
    lv_obj_t *keyboard_numeric;
    lv_obj_t *panel_setup_spinner;
    lv_obj_t *obj0;
    lv_obj_t *current;
    lv_obj_t *current__obj0;
    lv_obj_t *current__obj1;
    lv_obj_t *current__date_time;
    lv_obj_t *current__wifi;
    lv_obj_t *current__weather_icon;
    lv_obj_t *current__temp;
    lv_obj_t *current__obj2;
    lv_obj_t *current__humidity;
    lv_obj_t *current__pressure;
    lv_obj_t *current__clouds;
    lv_obj_t *current__uv;
    lv_obj_t *current__sunrise;
    lv_obj_t *current__sunset;
    lv_obj_t *current__wind_speed;
    lv_obj_t *current__wind_gust;
    lv_obj_t *current__wind_direction;
    lv_obj_t *sen66;
    lv_obj_t *sen66__obj3;
    lv_obj_t *sen66__obj4;
    lv_obj_t *sen66__icon;
    lv_obj_t *sen66__name;
    lv_obj_t *sen66__temp;
    lv_obj_t *sen66__obj5;
    lv_obj_t *sen66__humidity;
    lv_obj_t *sen66__container_pm;
    lv_obj_t *sen66__chart_pm;
    lv_obj_t *sen66__pm10;
    lv_obj_t *sen66__pm4;
    lv_obj_t *sen66__pm2p5;
    lv_obj_t *sen66__pm1;
    lv_obj_t *sen66__container_co2;
    lv_obj_t *sen66__chart_co2;
    lv_obj_t *sen66__co2;
    lv_obj_t *sen66__container_nox;
    lv_obj_t *sen66__chart_nox;
    lv_obj_t *sen66__nox;
    lv_obj_t *sen66__container_voc;
    lv_obj_t *sen66__chart_voc;
    lv_obj_t *sen66__voc;
    lv_obj_t *container_sensoren;
    lv_obj_t *sensor_0;
    lv_obj_t *sensor_0__obj6;
    lv_obj_t *sensor_0__header;
    lv_obj_t *sensor_0__icon;
    lv_obj_t *sensor_0__name;
    lv_obj_t *sensor_0__battery;
    lv_obj_t *sensor_0__wifi;
    lv_obj_t *sensor_0__temp;
    lv_obj_t *sensor_0__obj7;
    lv_obj_t *sensor_0__humidity;
    lv_obj_t *sensor_1;
    lv_obj_t *sensor_1__obj6;
    lv_obj_t *sensor_1__header;
    lv_obj_t *sensor_1__icon;
    lv_obj_t *sensor_1__name;
    lv_obj_t *sensor_1__battery;
    lv_obj_t *sensor_1__wifi;
    lv_obj_t *sensor_1__temp;
    lv_obj_t *sensor_1__obj7;
    lv_obj_t *sensor_1__humidity;
    lv_obj_t *sensor_2;
    lv_obj_t *sensor_2__obj6;
    lv_obj_t *sensor_2__header;
    lv_obj_t *sensor_2__icon;
    lv_obj_t *sensor_2__name;
    lv_obj_t *sensor_2__battery;
    lv_obj_t *sensor_2__wifi;
    lv_obj_t *sensor_2__temp;
    lv_obj_t *sensor_2__obj7;
    lv_obj_t *sensor_2__humidity;
    lv_obj_t *sensor_3;
    lv_obj_t *sensor_3__obj6;
    lv_obj_t *sensor_3__header;
    lv_obj_t *sensor_3__icon;
    lv_obj_t *sensor_3__name;
    lv_obj_t *sensor_3__battery;
    lv_obj_t *sensor_3__wifi;
    lv_obj_t *sensor_3__temp;
    lv_obj_t *sensor_3__obj7;
    lv_obj_t *sensor_3__humidity;
    lv_obj_t *sensor_4;
    lv_obj_t *sensor_4__obj10;
    lv_obj_t *sensor_4__header;
    lv_obj_t *sensor_4__icon;
    lv_obj_t *sensor_4__name;
    lv_obj_t *sensor_4__battery;
    lv_obj_t *sensor_4__wifi;
    lv_obj_t *sensor_4__temp;
    lv_obj_t *sensor_4__obj11;
    lv_obj_t *sensor_4__humidity;
    lv_obj_t *sensor_4__pressure;
    lv_obj_t *sensor_5;
    lv_obj_t *sensor_5__obj12;
    lv_obj_t *sensor_5__header;
    lv_obj_t *sensor_5__icon;
    lv_obj_t *sensor_5__name;
    lv_obj_t *sensor_5__battery;
    lv_obj_t *sensor_5__wifi;
    lv_obj_t *sensor_5__micro_sievert;
    lv_obj_t *sensor_5__obj13;
    lv_obj_t *sensor_5__container_m_sv;
    lv_obj_t *sensor_5__chart_m_sv;
    lv_obj_t *sensor_5__m_sv;
    lv_obj_t *hourly;
    lv_obj_t *hourly_chart;
    lv_obj_t *daily;
    lv_obj_t *daily_chart;
    lv_obj_t *message_box_update;
    lv_obj_t *update_version;
    lv_obj_t *button_update;
    lv_obj_t *progress_bar_update;
} objects_t;

extern objects_t objects;

void create_screen_setup_screen();
void tick_screen_setup_screen();

void create_screen_weatherstation_screen();
void tick_screen_weatherstation_screen();

void create_user_widget_current(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_current(int startWidgetIndex);

void create_user_widget_sensor_sen66(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_sen66(int startWidgetIndex);

void create_user_widget_sensor_temp_hum(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_temp_hum(int startWidgetIndex);

void create_user_widget_sensor_temp_hum_press(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_temp_hum_press(int startWidgetIndex);

void create_user_widget_sensor_temp_hum_press_compact(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_temp_hum_press_compact(int startWidgetIndex);

void create_user_widget_sensor_radiation(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_radiation(int startWidgetIndex);

void create_user_widget_sensor_radiation_compact(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_radiation_compact(int startWidgetIndex);

void create_user_widget_____________________________(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_____________________________(int startWidgetIndex);

void create_user_widget_sensor_kohlenmonoxid(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_kohlenmonoxid(int startWidgetIndex);

void create_user_widget_sensor_bme280_2(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_bme280_2(int startWidgetIndex);

void create_user_widget_sen66_widget_2(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sen66_widget_2(int startWidgetIndex);

void create_user_widget_base_6(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_6(int startWidgetIndex);

void create_user_widget_base_1(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_1(int startWidgetIndex);

void create_user_widget_base_2(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_2(int startWidgetIndex);

void create_user_widget_base_3(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_base_3(int startWidgetIndex);

void create_user_widget_sensor_sht45_1(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_sensor_sht45_1(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/