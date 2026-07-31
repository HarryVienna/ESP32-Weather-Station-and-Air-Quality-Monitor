#include "gui_sen66.h"

#include <math.h>
#include <stdio.h>

#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i18n/i18n.h"
#include "ui/ui.h"
#include "gui_color_scale.h"
#include "gui_history_chart.h"
#include "gui_icon_catalog.h"
#include "sensor_sen66_task.h"

/* ============================================================================
 * SEN66 - eingebauter Luftqualitaetssensor der Basisstation
 * ============================================================================ */

/* Threshold pointer arrays indexed by series id1, passed as user_data */
static const color_thresh_t *pm_thresh_arr[]  = {&THRESH_PM2P5};
static const color_thresh_t *voc_thresh_arr[] = {&THRESH_VOC};
static const color_thresh_t *nox_thresh_arr[] = {&THRESH_NOX};
static const color_thresh_t *co2_thresh_arr[] = {&THRESH_CO2};

static history_chart_t pm_chart, voc_chart, nox_chart, co2_chart;

void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2)
{
  lvgl_port_lock(0);

  if (!isnan(ambientTemperature))
  {
    char temp[8];
    sprintf(temp, "%.1f", ambientTemperature);
    lv_label_set_text(objects.sen66__temp, temp);
  }

  if (!isnan(ambientHumidity))
  {
    char humidity[8];
    sprintf(humidity, "%.1f", ambientHumidity);
    lv_label_set_text(objects.sen66__humidity, humidity);
  }

  lv_obj_set_style_bg_color(objects.sen66__pm1,   level_color_asc(massConcentrationPm1p0,  &THRESH_PM1),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm2p5, level_color_asc(massConcentrationPm2p5,  &THRESH_PM2P5), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm4,   level_color_asc(massConcentrationPm4p0,  &THRESH_PM4),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm10,  level_color_asc(massConcentrationPm10p0, &THRESH_PM10),  LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__voc,   level_color_asc(vocIndex,                &THRESH_VOC),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__nox,   level_color_asc(noxIndex,                &THRESH_NOX),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__co2,   level_color_asc((float)co2,              &THRESH_CO2),   LV_PART_MAIN | LV_STATE_DEFAULT);

  lvgl_port_unlock();
}

void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2)
{
    lvgl_port_lock(0);
    history_chart_push(&pm_chart,  pm2p5);
    history_chart_push(&voc_chart, voc);
    history_chart_push(&nox_chart, nox);
    history_chart_push(&co2_chart, (float)co2);
    lvgl_port_unlock();
}

void gui_sen66_init_charts(void)
{
  lv_obj_update_layout(objects.weatherstation_screen);

  // PM2.5: 0-75
  history_chart_init(&pm_chart, objects.sen66__chart_pm, 75,
                      lv_color_hex(0x616161), pm_thresh_arr, SEN66_HISTORY_SAMPLES_PER_DAY, 1.0f);

  // VOC: 0-500
  history_chart_init(&voc_chart, objects.sen66__chart_voc, 500,
                      lv_color_hex(0x1565C0), voc_thresh_arr, SEN66_HISTORY_SAMPLES_PER_DAY, 1.0f);

  // NOx: 0-400
  history_chart_init(&nox_chart, objects.sen66__chart_nox, 400,
                      lv_color_hex(0xE65100), nox_thresh_arr, SEN66_HISTORY_SAMPLES_PER_DAY, 1.0f);

  // CO2: 0-2000
  history_chart_init(&co2_chart, objects.sen66__chart_co2, 2000,
                      lv_color_hex(0x00695C), co2_thresh_arr, SEN66_HISTORY_SAMPLES_PER_DAY, 1.0f);
}

void gui_sen66_start_task(void)
{
  xTaskCreatePinnedToCore(
      sensor_sen66_task,
      "Sensor SEN66 Task",
      4096,
      NULL,
      1,
      NULL,
      1);
}

/* ============================================================================
 * Name+Icon der SEN66-Karte ("Basis" im Setup Screen) - Katalog siehe
 * gui_icon_catalog.h
 * ============================================================================ */

void load_basis_from_nvs(nvs_handle_t nvs_handle)
{
  populate_sensor_icon_dropdown(objects.basis_icon);
  uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, "icon_base", 0);
  lv_dropdown_set_selected(objects.basis_icon, icon_idx < sensor_icon_count() ? icon_idx : 0);
}

void save_basis_to_nvs(nvs_handle_t nvs_handle)
{
  uint8_t icon_idx = lv_dropdown_get_selected(objects.basis_icon);
  put_uint8_to_nvs(nvs_handle, "icon_base", icon_idx);
}

void apply_sen66_config(void)
{
  uint8_t basis_icon_idx = lv_dropdown_get_selected(objects.basis_icon);
  const sensor_icon_option_t *opt = sensor_icon_option(basis_icon_idx);
  lv_label_set_text(objects.sen66__name, _(opt->label));
  lv_image_set_src(objects.sen66__icon, opt->icon);
}
