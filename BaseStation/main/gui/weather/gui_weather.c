#include "gui_weather.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui/ui.h"
#include "lvgl/lv_common.h"
#include "lvgl/lv_hourly_chart.h"
#include "lvgl/lv_daily_chart.h"
#include "weather_task.h"

static const char* TAG = "gui_weather";

#define NUM_ICONS 28

typedef struct
{
  const uint8_t icon;
  const lv_img_dsc_t *icon_image;
} icon_mapping_t;

static icon_mapping_t icon_mapping_day[] = {
      {0, &img_day_0},
      {1, &img_day_1},
      {2, &img_day_2},
      {3, &img_day_3},
      {45, &img_day_45},
      {48, &img_day_48},
      {51, &img_day_51},
      {53, &img_day_53},
      {55, &img_day_55},
      {56, &img_day_56},
      {57, &img_day_57},
      {61, &img_day_61},
      {63, &img_day_63},
      {65, &img_day_65},
      {66, &img_day_66},
      {67, &img_day_67},
      {71, &img_day_71},
      {73, &img_day_73},
      {75, &img_day_75},
      {77, &img_day_77},
      {80, &img_day_80},
      {81, &img_day_81},
      {82, &img_day_82},
      {85, &img_day_85},
      {86, &img_day_86},
      {95, &img_day_95},
      {96, &img_day_96},
      {99, &img_day_99}};

static icon_mapping_t icon_mapping_night[] = {
      {0, &img_night_0},
      {1, &img_night_1},
      {2, &img_night_2},
      {3, &img_night_3},
      {45, &img_night_45},
      {48, &img_night_48},
      {51, &img_night_51},
      {53, &img_night_53},
      {55, &img_night_55},
      {56, &img_night_56},
      {57, &img_night_57},
      {61, &img_night_61},
      {63, &img_night_63},
      {65, &img_night_65},
      {66, &img_night_66},
      {67, &img_night_67},
      {71, &img_night_71},
      {73, &img_night_73},
      {75, &img_night_75},
      {77, &img_night_77},
      {80, &img_night_80},
      {81, &img_night_81},
      {82, &img_night_82},
      {85, &img_night_85},
      {86, &img_night_86},
      {95, &img_night_95},
      {96, &img_night_96},
      {99, &img_night_99}};

void disp_weather(current_weather_data_t *current_weather, hourly_weather_data_t *hourly_weather, daily_weather_data_t *daily_weather) {
  lvgl_port_lock(0);

  // Current data
  char temp[8];
  char humidity[8];
  char pressure[8];
  char clouds[8];
  char uv_index[8];
  char wind_speed[8];
  char wind_gust[8];
  char str_sunrise[8];
  char str_sunset[8];

  icon_mapping_t *icon_mapping;
  if (current_weather->is_day) {
    icon_mapping = icon_mapping_day;
  }
  else {
    icon_mapping = icon_mapping_night;
  }

  for (uint8_t i = 0; i < NUM_ICONS; i++)
  {
    if (current_weather->weather_code == icon_mapping[i].icon)
    {
      ESP_LOGI(TAG, "Weather code %d", current_weather->weather_code);
      lv_img_set_src(objects.current__weather_icon, icon_mapping[i].icon_image);
      break;
    }
  }

  sprintf(temp, "%.1f", current_weather->temperature_2m);
  lv_label_set_text(objects.current__temp, temp);

  sprintf(humidity, "%d", current_weather->relative_humidity_2m);
  lv_label_set_text(objects.current__humidity, humidity);

  sprintf(pressure, "%.0f", current_weather->pressure_msl);
  lv_label_set_text(objects.current__pressure, pressure);

  sprintf(clouds, "%d", current_weather->cloud_cover);
  lv_label_set_text(objects.current__clouds, clouds);

  sprintf(uv_index, "%d", (int) round(current_weather->uv_index));
  lv_label_set_text(objects.current__uv, uv_index);

  sprintf(wind_speed, "%.1f", current_weather->wind_speed_10m);
  lv_label_set_text(objects.current__wind_speed, wind_speed);

  sprintf(wind_gust, "%.1f", current_weather->wind_gusts_10m);
  lv_label_set_text(objects.current__wind_gust, wind_gust);

  lv_img_set_angle(objects.current__wind_direction, current_weather->wind_direction_10m * 10);

  struct tm time_sunrise = current_weather->sunrise;
  struct tm time_sunrset = current_weather->sunset;
  strftime(str_sunrise, sizeof(str_sunrise), "%H:%M", &time_sunrise);
  lv_label_set_text(objects.current__sunrise, str_sunrise);
  strftime(str_sunset, sizeof(str_sunset), "%H:%M", &time_sunrset);
  lv_label_set_text(objects.current__sunset, str_sunset);

  // Hourly data
  lv_hourly_data hourly_data[NUM_HOURS];

  for (int i = 0; i < NUM_HOURS; i++)
  {
    hourly_data[i].dt = hourly_weather[i].time;
    hourly_data[i].temp = hourly_weather[i].temperature_2m;
    hourly_data[i].dew = hourly_weather[i].dew_point_2m;
    hourly_data[i].rain = hourly_weather[i].rain + hourly_weather[i].showers;
    hourly_data[i].snow = hourly_weather[i].snowfall * 10.0f / 7.0f;  // See docu from open-meteo.com  snow -> water
    hourly_data[i].pop = hourly_weather[i].precipitation_probability;
    //hourly_data[i].pop = (hourly_weather[i].precipitation_probability * 80.0f / 100.0f + 20.0f) / 100.0f;  // Map 0-100 to 25-100 for better visualisation
    hourly_data[i].sun = hourly_weather[i].sunshine_duration / 3600.0f;
    //hourly_data[i].sun = hourly_weather[i].is_day ? (100.0f - source_data[i].cloud_cover) / 100.0f : 0;
  }

  lv_hourly_chart_set_data(objects.hourly_chart, hourly_data);
  lv_hourly_chart_refresh(objects.hourly_chart);

  // Daily data
  lv_daily_data daily_data[NUM_DAYS];

  for (int i = 0; i < NUM_DAYS; i++)
  {
    daily_data[i].dt = daily_weather[i].time;
    daily_data[i].low_temp = daily_weather[i].temperature_2m_min;
    daily_data[i].high_temp = daily_weather[i].temperature_2m_max;
    daily_data[i].rain = daily_weather[i].rain_sum + daily_weather[i].showers_sum;
    daily_data[i].snow = daily_weather[i].snowfall_sum * 10.0f / 7.0f;  // See docu from open-meteo.com  snow -> water
    daily_data[i].pop = daily_weather[i].precipitation_probability_max;
    daily_data[i].sun = daily_weather[i].sunshine_duration / daily_weather[i].daylight_duration;
  }

  lv_daily_chart_set_data(objects.daily_chart, daily_data);
  lv_daily_chart_refresh(objects.daily_chart);

  lvgl_port_unlock();
}

void gui_weather_init_charts(void)
{
  lv_obj_update_layout(objects.weatherstation_screen);

  {
    lv_obj_t *placeholder = objects.hourly_chart;
    lv_obj_t *parent_obj = lv_obj_get_parent(placeholder);

    lv_obj_t *obj = lv_hourly_chart_create(parent_obj);
    objects.hourly_chart = obj;
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, lv_pct(100));
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(obj, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 5, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 5, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(obj, 0, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(obj, 10, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, 255, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, &ui_font_free_sans20, LV_PART_TICKS | LV_STATE_DEFAULT);

    lv_obj_del(placeholder);
  }

  {
    lv_obj_t *placeholder = objects.daily_chart;
    lv_obj_t *parent_obj = lv_obj_get_parent(placeholder);

    lv_obj_t *obj = lv_daily_chart_create(parent_obj);
    objects.daily_chart = obj;
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, lv_pct(100));
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(obj, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(obj, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 5, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 5, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(obj, 0, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(obj, 10, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, 255, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, &ui_font_free_sans20, LV_PART_TICKS | LV_STATE_DEFAULT);

    lv_obj_del(placeholder);
  }
}

void gui_weather_start_task(void)
{
  xTaskCreatePinnedToCore(
      weather_task,
      "Weather Task",
      16384,
      NULL,
      1,
      NULL,
      1);
}
