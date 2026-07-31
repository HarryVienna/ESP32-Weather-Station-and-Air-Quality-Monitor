#include "gui_sensors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_lvgl_port.h"

#include "config/config.h"
#include "i18n/i18n.h"
#include "ui/ui.h"
#include "gui_color_scale.h"
#include "gui_history_chart.h"
#include "gui_icon_catalog.h"

/* ============================================================================
 * Sensors - 6 remote sensors (LoRa/ESP-NOW)
 *
 * See the comment at the top of gui_sensors.h for the short version. Order here:
 *   - calc_sea_level_pressure()      Helper calculation for BME280 air pressure
 *   - sensor_dropdown_widgets[]      Setup screen dropdowns, one entry/slot
 *   - load/save_sensor_slots_*_nvs() Dropdown selection <-> flash
 *   - render_xxx()                   One render() per widget type (Temp_Hum/-Press/Radiation)
 *   - sensor_slots[]                 b) Hardware mapping: type + generated
 *                                       field names per slot in the Weatherstation screen
 *   - apply_sensor_slot_configs()    Setup selection -> Weatherstation screen
 *   - disp_sensor_link_quality()     Colors the battery/signal icon
 *   - disp_sensor_values()           Displays a packet's readings
 * ============================================================================ */

/**
 * @brief     Calculate sea level pressure based on provided parameters
 *
 * @param     pressure      Atmospheric pressure at the measurement point (in hPa)
 * @param     temperature   Temperature at the measurement point (in Celsius)
 * @param     altitude      Altitude above sea level (in meters)
 *
 * @return    float         Sea level pressure calculated based on the parameters (in hPa)
 *
 * @details   Calculates and estimates the sea level pressure using the barometric formula.
 *            Incorporates constants and calculations to adjust the pressure for altitude and temperature.
 */
static float calc_sea_level_pressure(float pressure, float temperature, uint16_t altitude)
{
  // https://en.wikipedia.org/wiki/Barometric_formula

  // Constants
  float g = 9.80665;  // gravitational acceleration in m/s^2
  float R = 287.05;   // specific gas constant of dry air (= R/M) in m^2/(s^2*K)
  float a = 0.0065;   // vertical temperature gradient
  float C_h = 0.12;   // coefficient accounting for the mean vapor pressure change, K/hPa
  float T_0 = 273.15; // Celsius to Kelvin

  float E; // vapor pressure of the water vapor fraction (in hPa)

  if (temperature < 9.1)
  {
    E = 5.6402 * (-0.0916 + exp(0.06 * temperature));
  }
  else
  {
    E = 18.2194 * (1.0463 + exp(-0.0666 * temperature));
  }

  // calculate the sea level pressure
  float p = pressure * exp(altitude * g / (R * (temperature + T_0 + C_h * E + a * (altitude / 2))));

  return p;
}

/* One dropdown per slot sets name AND icon together - the selected index
 * points directly into the catalog from gui_icon_catalog.h (label =
 * display name, icon = image). There are no separate name fields in the
 * setup screen. Index 0-5 here = Sensor 1-6 in the UI =
 * packet_header_t.sensor_nr in the radio packet. */
static lv_obj_t **const sensor_dropdown_widgets[SENSOR_SLOT_COUNT] = {
    &objects.sensor_0_name, &objects.sensor_1_name, &objects.sensor_2_name,
    &objects.sensor_3_name, &objects.sensor_4_name, &objects.sensor_5_name,
};

void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle)
{
  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    char key[20];

    populate_sensor_icon_dropdown(*sensor_dropdown_widgets[i]);
    snprintf(key, sizeof(key), "sensor%d_icon", i);
    uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, key, 0);
    lv_dropdown_set_selected(*sensor_dropdown_widgets[i], icon_idx < sensor_icon_count() ? icon_idx : 0);
  }
}

void save_sensor_slots_to_nvs(nvs_handle_t nvs_handle)
{
  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    char key[20];

    uint8_t icon_idx = lv_dropdown_get_selected(*sensor_dropdown_widgets[i]);
    snprintf(key, sizeof(key), "sensor%d_icon", i);
    put_uint8_to_nvs(nvs_handle, key, icon_idx);
  }
}

/* b) Hardware mapping: each of the 6 Sensor_X widgets (X=0..5, named that
 * way directly in EEZ Studio) permanently has a fixed widget type
 * (currently: Sensor_0/1/2/3=Sensor_Temp_Hum, Sensor_4=Sensor_Temp_Hum_Press,
 * Sensor_5=Sensor_Radiation; see screens.c for the create_user_widget_*()
 * calls actually instantiated per slot). This table is the ONLY place that
 * needs adjusting when the widget type installed in a slot changes in EEZ
 * Studio - just swap in the new field names (see screens.h for the actual
 * generated objects.sensor_N__xxx names). As long as the 6 widgets keep
 * their instance names (Sensor_0..Sensor_5) in EEZ Studio, these field
 * names stay stable regardless of what else changes in the layout.
 *
 * The EEZ Studio widget types (Sensor_Temp_Hum, Sensor_Temp_Hum_Press, ...)
 * are pure layouts and deliberately NOT named after sensor hardware - temp+
 * humidity look the same regardless of the sensor (SHT45, BME280, ...).
 * Each widget type has its own render() function that evaluates the passed
 * sensor_type_t itself via a switch to cast to the right payload struct -
 * that way the same render_temp_hum() can be attached to either an SHT45
 * or a BME280 slot, without the caller having to keep sensor hardware and
 * cast in sync by hand anywhere (a mismatch there would cast to the wrong
 * payload struct and swap fields, e.g. showing pressure where temperature
 * belongs). sensor_type also still serves as the slot-level packet type
 * check (a packet that doesn't match the sensor installed at this slot is
 * ignored before render() is even called). */
typedef struct {
  lv_obj_t **value1;   /* Temp (bme280/sht45) or uSv/h (geiger) */
  lv_obj_t **value2;   /* Humidity (bme280/sht45), else NULL */
  lv_obj_t **value3;   /* Pressure (Temp_Hum_Press/-Compact only), else NULL */
  lv_obj_t **chart;    /* 24h history chart (geiger only), else NULL */
  lv_obj_t **quality;  /* Color panel (geiger only), else NULL */
} sensor_values_t;

typedef void (*sensor_render_fn_t)(sensor_type_t type, const sensor_values_t *values, const void *payload);

typedef struct {
  sensor_type_t type;          /* expected packet type - for the type check */
  sensor_render_fn_t render;   /* widget type: knows which values get filled in */
  lv_obj_t **name;
  lv_obj_t **icon;
  lv_obj_t **battery;
  lv_obj_t **wifi;
  lv_obj_t **header;           /* card header row - colored red when "offline" */
  sensor_values_t values;
} sensor_slot_t;

/* lv_label_set_text_fmt()/lv_snprintf() don't support float format
 * specifiers here (CONFIG_LV_USE_FLOAT is off, only LV_USE_BUILTIN_SPRINTF) -
 * "%.1f" etc. would just print garbage/"f". So, as elsewhere in
 * gui_sensors.c, format with libc sprintf into a buffer and set the
 * finished string. */

static void render_temp_hum(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  float temperature, humidity;
  switch (type) {
    case SENSOR_TYPE_SHT45: {
      const sht45_payload_t *d = (const sht45_payload_t *)payload;
      temperature = d->temperature;
      humidity = d->humidity;
      break;
    }
    case SENSOR_TYPE_BME280: {
      const bme280_payload_t *d = (const bme280_payload_t *)payload;
      temperature = d->temperature;
      humidity = d->humidity;
      break;
    }
    default:
      return;   /* sensor doesn't provide temp/humidity values */
  }

  char buf[16];
  sprintf(buf, "%.1f", temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.1f", humidity);
  lv_label_set_text(*v->value2, buf);
}

static void render_temp_hum_press(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  float temperature, humidity, pressure;
  switch (type) {
    case SENSOR_TYPE_BME280: {
      const bme280_payload_t *d = (const bme280_payload_t *)payload;
      temperature = d->temperature;
      humidity = d->humidity;
      pressure = d->pressure;
      break;
    }
    default:
      return;   /* sensor doesn't provide a pressure value */
  }

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
  nvs_close(nvs_handle);
  float sea_level_pressure = calc_sea_level_pressure(pressure, temperature, (uint16_t)atol(height_c));

  char buf[16];
  sprintf(buf, "%.1f", temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.0f", humidity);
  lv_label_set_text(*v->value2, buf);
  sprintf(buf, "%.0f", sea_level_pressure);
  lv_label_set_text(*v->value3, buf);
}

/* Like render_temp_hum_press(), but humidity without a decimal place -
 * the Sensor_Temp_Hum_Press_Compact widget has less room for it. Not
 * assigned to any slot yet (see sensor_slots[] below). */
static void render_temp_hum_press_compact(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  float temperature, humidity, pressure;
  switch (type) {
    case SENSOR_TYPE_BME280: {
      const bme280_payload_t *d = (const bme280_payload_t *)payload;
      temperature = d->temperature;
      humidity = d->humidity;
      pressure = d->pressure;
      break;
    }
    default:
      return;
  }

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
  nvs_close(nvs_handle);
  float sea_level_pressure = calc_sea_level_pressure(pressure, temperature, (uint16_t)atol(height_c));

  char buf[16];
  sprintf(buf, "%.1f", temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.0f", humidity);
  lv_label_set_text(*v->value2, buf);
  sprintf(buf, "%.0f", sea_level_pressure);
  lv_label_set_text(*v->value3, buf);
}

/* ============================================================================
 * Geigerzaehler - 24h-Verlaufschart (Slot 5, "Strahlung")
 *
 * Uses history_chart_t (see gui_history_chart.h) for the peak bucketing:
 * the sensor sends 1x/minute (see geiger_payload_t), but the chart widget
 * is only about 130px wide - history_chart_init() derives from that how
 * many per-minute readings get combined into one bar (rounded up), so the
 * full chart width always covers >= 24h (RADIATION_HISTORY_SAMPLES_PER_DAY,
 * see gui_sensors.h) instead of scrolling through after point_count minutes.
 * ============================================================================ */
static const color_thresh_t *radiation_thresh_arr[] = { &THRESH_RADIATION };
static history_chart_t radiation_chart;

static void render_radiation(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  float usvh;
  switch (type) {
    case SENSOR_TYPE_GEIGER: {
      const geiger_payload_t *d = (const geiger_payload_t *)payload;
      usvh = d->usvh;
      break;
    }
    default:
      return;
  }

  char buf[16];
  sprintf(buf, "%.2f", usvh);
  lv_label_set_text(*v->value1, buf);

  /* THRESH_RADIATION is defined in centi-uSv/h (see gui_color_scale.h), hence *100 */
  lv_obj_set_style_bg_color(*v->quality, level_color_asc(usvh * 100.0f, &THRESH_RADIATION), LV_PART_MAIN | LV_STATE_DEFAULT);

  history_chart_push(&radiation_chart, usvh);
}

/**
 * @brief  Initializes the Geiger counter's 24h history chart (slot 5).
 *
 * @details Like gui_sen66_init_charts() (see gui_sen66.h): call once at
 *          startup, before packets start coming in via
 *          disp_sensor_values().
 */
void gui_radiation_init_chart(void)
{
  lv_obj_update_layout(objects.weatherstation_screen);

  // 0-100 = 0.00-1.00 uSv/h, see THRESH_RADIATION
  history_chart_init(&radiation_chart, objects.sensor_5__chart_m_sv, 100,
                      lv_color_hex(0x616161), radiation_thresh_arr, RADIATION_HISTORY_SAMPLES_PER_DAY, 100.0f);
}

static const sensor_slot_t sensor_slots[SENSOR_SLOT_COUNT] = {
    // Bedroom
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_0__name, &objects.sensor_0__icon, &objects.sensor_0__battery, &objects.sensor_0__wifi,
      &objects.sensor_0__header, { &objects.sensor_0__temp, &objects.sensor_0__humidity, NULL, NULL, NULL } },
    // Bathroom
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_1__name, &objects.sensor_1__icon, &objects.sensor_1__battery, &objects.sensor_1__wifi,
      &objects.sensor_1__header, { &objects.sensor_1__temp, &objects.sensor_1__humidity, NULL, NULL, NULL } },
    // Office
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_2__name, &objects.sensor_2__icon, &objects.sensor_2__battery, &objects.sensor_2__wifi,
      &objects.sensor_2__header, { &objects.sensor_2__temp, &objects.sensor_2__humidity, NULL, NULL, NULL } },
    // Workshop
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_3__name, &objects.sensor_3__icon, &objects.sensor_3__battery, &objects.sensor_3__wifi,
      &objects.sensor_3__header, { &objects.sensor_3__temp, &objects.sensor_3__humidity, NULL, NULL, NULL } },
    // Balcony
    { SENSOR_TYPE_BME280, render_temp_hum_press, &objects.sensor_4__name, &objects.sensor_4__icon, &objects.sensor_4__battery, &objects.sensor_4__wifi,
      &objects.sensor_4__header, { &objects.sensor_4__temp, &objects.sensor_4__humidity, &objects.sensor_4__pressure, NULL, NULL } },
    // Geiger counter
    { SENSOR_TYPE_GEIGER, render_radiation,      &objects.sensor_5__name, &objects.sensor_5__icon, &objects.sensor_5__battery, &objects.sensor_5__wifi,
      &objects.sensor_5__header, { &objects.sensor_5__micro_sievert, NULL, NULL, &objects.sensor_5__chart_m_sv, &objects.sensor_5__m_sv } },
};

/* New widget type, or a slot switching to an existing type (e.g. switching
 * a slot to Sensor_Temp_Hum_Press_Compact): enter the matching
 * render_xxx() function above and update the objects.sensor_N__xxx fields
 * below based on screens.h (note: EEZ Studio may append suffixes like
 * _1/_2 to field names when a screen has multiple widget instances with
 * the same label - see sensor_4 above). */

/**
 * @brief  Applies name/icon from the setup screen to the 6 hardwired
 *         sensor cards. Called on clicking "Start" right next to
 *         apply_sen66_config() (see gui_sen66.h), before switching to the
 *         Weatherstation screen.
 */
void apply_sensor_slot_configs(void)
{
  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    uint8_t icon_idx = lv_dropdown_get_selected(*sensor_dropdown_widgets[i]);
    const sensor_icon_option_t *opt = sensor_icon_option(icon_idx);
    lv_label_set_text(*sensor_slots[i].name, _(opt->label));
    lv_image_set_src(*sensor_slots[i].icon, opt->icon);
  }
}

/**
 * @brief  Colors a sensor card's battery and signal icon based on voltage/RSSI.
 *
 * @param  sensor_nr   0-5, as in the packet header (packet_header_t.sensor_nr) -
 *                     identical to the 0-based UI slot (Sensor 0-5)
 * @param  voltage_mv  battery voltage in mV, from the respective payload
 * @param  rssi_dbm    receive signal strength in dBm, from link_metadata_t.rssi
 */
void disp_sensor_link_quality(uint8_t sensor_nr, uint32_t voltage_mv, int16_t rssi_dbm)
{
  if (sensor_nr >= SENSOR_SLOT_COUNT) {
    return;
  }
  const sensor_slot_t *slot = &sensor_slots[sensor_nr];

  lv_color_t battery_color = level_color_desc(voltage_mv / 1000.0f, &THRESH_BATTERY_VOLTAGE);
  lv_color_t signal_color = level_color_desc((float)rssi_dbm, &THRESH_RSSI_DBM);

  lvgl_port_lock(0);
  lv_obj_set_style_img_recolor(*slot->battery, battery_color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(*slot->battery, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor(*slot->wifi, signal_color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(*slot->wifi, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lvgl_port_unlock();
}

/**
 * @brief  Writes a received packet's readings to the sensor card.
 *
 * @param  sensor_nr  0-5, see disp_sensor_link_quality()
 * @param  type       sensor_type_t of the received packet
 * @param  payload    raw payload (bme280_payload_t/sht45_payload_t/geiger_payload_t,
 *                     depending on type)
 *
 * @details If the reported type doesn't match the type hardwired to this
 *          card (e.g. sensor misconfigured/miswired), the packet is
 *          ignored instead of filling in a mislabeled card.
 */
void disp_sensor_values(uint8_t sensor_nr, sensor_type_t type, const void *payload)
{
  if (sensor_nr >= SENSOR_SLOT_COUNT) {
    return;
  }
  const sensor_slot_t *slot = &sensor_slots[sensor_nr];
  if (slot->type != type) {
    return;
  }

  lvgl_port_lock(0);
  slot->render(type, &slot->values, payload);
  lvgl_port_unlock();
}

/**
 * @brief  Colors a sensor card's header row red (the receiver's watchdog
 *         hasn't gotten a packet from this sensor in a while), or
 *         reverses that once the sensor sends again.
 *
 * @param  sensor_nr  0-5, see disp_sensor_link_quality()
 * @param  offline    true = color red, false = restore normal state
 */
void disp_sensor_offline(uint8_t sensor_nr, bool offline)
{
  if (sensor_nr >= SENSOR_SLOT_COUNT) {
    return;
  }
  const sensor_slot_t *slot = &sensor_slots[sensor_nr];

  lvgl_port_lock(0);
  if (offline) {
    lv_obj_set_style_bg_color(*slot->header, lv_color_hex(COLOR_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(*slot->header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else {
    lv_obj_set_style_bg_opa(*slot->header, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  lvgl_port_unlock();
}
