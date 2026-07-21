#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_sensor_home;
extern const lv_img_dsc_t img_sensor_bathroom;
extern const lv_img_dsc_t img_sensor_bedroom;
extern const lv_img_dsc_t img_sensor_kitchen;
extern const lv_img_dsc_t img_sensor_balcony;
extern const lv_img_dsc_t img_sensor_cellar;
extern const lv_img_dsc_t img_sensor_office;
extern const lv_img_dsc_t img_sensor_workshop;
extern const lv_img_dsc_t img_sensor_radiation;
extern const lv_img_dsc_t img_wifi;
extern const lv_img_dsc_t img_battery;
extern const lv_img_dsc_t img_humidity;
extern const lv_img_dsc_t img_pressure;
extern const lv_img_dsc_t img_cloud;
extern const lv_img_dsc_t img_uv;
extern const lv_img_dsc_t img_sunrise;
extern const lv_img_dsc_t img_sunset;
extern const lv_img_dsc_t img_wind;
extern const lv_img_dsc_t img_gust;
extern const lv_img_dsc_t img_arrow;
extern const lv_img_dsc_t img_direction;
extern const lv_img_dsc_t img_day_0;
extern const lv_img_dsc_t img_night_0;
extern const lv_img_dsc_t img_day_1;
extern const lv_img_dsc_t img_night_1;
extern const lv_img_dsc_t img_day_2;
extern const lv_img_dsc_t img_night_2;
extern const lv_img_dsc_t img_day_3;
extern const lv_img_dsc_t img_night_3;
extern const lv_img_dsc_t img_day_45;
extern const lv_img_dsc_t img_night_45;
extern const lv_img_dsc_t img_day_48;
extern const lv_img_dsc_t img_night_48;
extern const lv_img_dsc_t img_day_51;
extern const lv_img_dsc_t img_night_51;
extern const lv_img_dsc_t img_day_53;
extern const lv_img_dsc_t img_night_53;
extern const lv_img_dsc_t img_day_55;
extern const lv_img_dsc_t img_night_55;
extern const lv_img_dsc_t img_day_56;
extern const lv_img_dsc_t img_night_56;
extern const lv_img_dsc_t img_day_57;
extern const lv_img_dsc_t img_night_57;
extern const lv_img_dsc_t img_day_61;
extern const lv_img_dsc_t img_night_61;
extern const lv_img_dsc_t img_day_63;
extern const lv_img_dsc_t img_night_63;
extern const lv_img_dsc_t img_day_65;
extern const lv_img_dsc_t img_night_65;
extern const lv_img_dsc_t img_day_66;
extern const lv_img_dsc_t img_night_66;
extern const lv_img_dsc_t img_day_67;
extern const lv_img_dsc_t img_night_67;
extern const lv_img_dsc_t img_day_71;
extern const lv_img_dsc_t img_night_71;
extern const lv_img_dsc_t img_day_73;
extern const lv_img_dsc_t img_night_73;
extern const lv_img_dsc_t img_day_75;
extern const lv_img_dsc_t img_night_75;
extern const lv_img_dsc_t img_day_77;
extern const lv_img_dsc_t img_night_77;
extern const lv_img_dsc_t img_day_80;
extern const lv_img_dsc_t img_night_80;
extern const lv_img_dsc_t img_day_81;
extern const lv_img_dsc_t img_night_81;
extern const lv_img_dsc_t img_day_82;
extern const lv_img_dsc_t img_night_82;
extern const lv_img_dsc_t img_day_85;
extern const lv_img_dsc_t img_night_85;
extern const lv_img_dsc_t img_day_86;
extern const lv_img_dsc_t img_night_86;
extern const lv_img_dsc_t img_day_95;
extern const lv_img_dsc_t img_night_95;
extern const lv_img_dsc_t img_day_96;
extern const lv_img_dsc_t img_night_96;
extern const lv_img_dsc_t img_day_99;
extern const lv_img_dsc_t img_night_99;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[77];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/