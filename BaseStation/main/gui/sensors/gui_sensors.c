#include "gui_sensors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/config.h"
#include "ui/ui.h"
#include "sensor_sen66_task.h"

/* ============================================================================
 * Farbskalen - gemeinsame Schwellwert-Helfer
 *
 * Zwei Formen, je nachdem ob ein hoeherer Messwert besser oder schlechter
 * ist. Beide geben COLOR_GREEN..COLOR_RED zurueck (siehe config.h). Die
 * Schwellwerte selbst (color_thresh_t-Instanzen) liegen in config.h, da sie
 * reine Konfiguration ohne Framework-Bezug sind.
 * ============================================================================ */

/* Absteigend: t1 = bester (hoechster) Wert. Fuer Messgroessen bei denen ein
 * hoeherer Wert besser ist (Akkuspannung, RSSI). Deklaration in
 * gui_sensors.h, da auch von gui_status.c gebraucht. */
lv_color_t level_color_desc(float val, const color_thresh_t *t)
{
    if      (val >= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val >= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val >= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val >= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}

/* Aufsteigend: t1 = bester (niedrigster) Wert. Fuer Messgroessen bei denen
 * ein hoeherer Wert schlechter ist (Feinstaub, VOC, NOx, CO2). Nur intern
 * fuer SEN66-Werte gebraucht, deshalb static. */
static lv_color_t sen66_value_color(float val, const color_thresh_t *t)
{
    if      (val <= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val <= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val <= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val <= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}


/* ============================================================================
 * SEN66 - eingebauter Luftqualitaetssensor der Basisstation
 *
 * Name/Icon der SEN66-Karte werden wie bei den Fernsensoren im Setup Screen
 * konfiguriert (siehe apply_slot_configs() weiter unten) - hier geht es nur
 * um die eigentlichen Messwerte (Temp/Feuchte/Feinstaub/VOC/NOx/CO2).
 * ============================================================================ */

/* Threshold pointer arrays indexed by series id1, passed as user_data */
static const color_thresh_t *pm_thresh_arr[]  = {&THRESH_PM2P5};
static const color_thresh_t *voc_thresh_arr[] = {&THRESH_VOC};
static const color_thresh_t *nox_thresh_arr[] = {&THRESH_NOX};
static const color_thresh_t *co2_thresh_arr[] = {&THRESH_CO2};

static lv_chart_series_t *ser_pm2p5 = NULL;
static lv_chart_series_t *ser_voc   = NULL;
static lv_chart_series_t *ser_nox   = NULL;
static lv_chart_series_t *ser_co2   = NULL;

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

  lv_obj_set_style_bg_color(objects.sen66__pm1,   sen66_value_color(massConcentrationPm1p0,  &THRESH_PM1),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm2p5, sen66_value_color(massConcentrationPm2p5,  &THRESH_PM2P5), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm4,   sen66_value_color(massConcentrationPm4p0,  &THRESH_PM4),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm10,  sen66_value_color(massConcentrationPm10p0, &THRESH_PM10),  LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__voc,   sen66_value_color(vocIndex,                &THRESH_VOC),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__nox,   sen66_value_color(noxIndex,                &THRESH_NOX),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__co2,   sen66_value_color((float)co2,              &THRESH_CO2),   LV_PART_MAIN | LV_STATE_DEFAULT);

  lvgl_port_unlock();
}

void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2)
{
    lvgl_port_lock(0);
    lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm2p5, (int32_t)pm2p5);
    lv_chart_set_next_value(objects.sen66__chart_voc, ser_voc,  (int32_t)voc);
    lv_chart_set_next_value(objects.sen66__chart_nox, ser_nox,  (int32_t)nox);
    lv_chart_set_next_value(objects.sen66__chart_co2, ser_co2,  (float)co2);
    lvgl_port_unlock();
}

/* BAR chart callback for CO2/VOC/NOx — recolors each 1-px bar by threshold */
static void sen66_bar_fill_cb(lv_event_t *e)
{
    lv_draw_task_t *draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
    if (base_dsc->part != LV_PART_ITEMS) return;

    lv_draw_fill_dsc_t *fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
    if (!fill_dsc) return;

    lv_obj_t *chart = lv_event_get_target_obj(e);
    lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
    if (!ser) return;

    uint32_t  pt_cnt = lv_chart_get_point_count(chart);
    uint32_t  start  = lv_chart_get_x_start_point(chart, ser);
    int32_t  *y      = lv_chart_get_series_y_array(chart, ser);
    int32_t   val    = y[(start + base_dsc->id2) % pt_cnt];
    if (val == LV_CHART_POINT_NONE) return;

    const color_thresh_t *thresh = ((const color_thresh_t **)lv_event_get_user_data(e))[0];
    fill_dsc->color = sen66_value_color((float)val, thresh);
}

void gui_sen66_init_charts(void)
{
  lv_obj_update_layout(objects.weatherstation_screen);

  // PM2.5: 0-75, 1-px BAR
  {
    lv_obj_t *chart = objects.sen66__chart_pm;
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 75);
    lv_chart_set_point_count(chart, lv_obj_get_width(chart));
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ser_pm2p5 = lv_chart_add_series(chart, lv_color_hex(0x616161), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(chart, sen66_bar_fill_cb, LV_EVENT_DRAW_TASK_ADDED, pm_thresh_arr);
  }

  // VOC: 0-500, 1-px BAR
  {
    lv_obj_t *chart = objects.sen66__chart_voc;
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    lv_chart_set_point_count(chart, lv_obj_get_width(chart));
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ser_voc = lv_chart_add_series(chart, lv_color_hex(0x1565C0), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(chart, sen66_bar_fill_cb, LV_EVENT_DRAW_TASK_ADDED, voc_thresh_arr);
  }

  // NOx: 0-400, 1-px BAR
  {
    lv_obj_t *chart = objects.sen66__chart_nox;
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 400);
    lv_chart_set_point_count(chart, lv_obj_get_width(chart));
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ser_nox = lv_chart_add_series(chart, lv_color_hex(0xE65100), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(chart, sen66_bar_fill_cb, LV_EVENT_DRAW_TASK_ADDED, nox_thresh_arr);
  }

  // CO2: 0-2000, 1-px BAR
  {
    lv_obj_t *chart = objects.sen66__chart_co2;
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2000);
    lv_chart_set_point_count(chart, lv_obj_get_width(chart));
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ser_co2 = lv_chart_add_series(chart, lv_color_hex(0x00695C), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(chart, sen66_bar_fill_cb, LV_EVENT_DRAW_TASK_ADDED, co2_thresh_arr);
  }

  /* TEST: pre-fill 140 points — remove before release */
  // for (int i = 0; i < 140; i++) {
  //     float s = sinf(i * 0.12f);
  //     float c = cosf(i * 0.07f);
  //     lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm2p5, (int32_t)( 20 +  70 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_voc, ser_voc,  (int32_t)(120 + 60 * c));
  //     lv_chart_set_next_value(objects.sen66__chart_nox, ser_nox,  (int32_t)(  5 + 200 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_co2, ser_co2,  (int32_t)(800 + 200 * c));
  // }
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
 * Sensoren - 6 Fernsensoren (LoRa/ESP-NOW)
 *
 * Siehe Kommentar am Kopf von gui_sensors.h fuer die Kurzfassung. Reihenfolge hier:
 *   - calc_sea_level_pressure()      Hilfsrechnung fuer BME280-Luftdruck
 *   - sensor_icon_options[]          a) Name+Icon-Katalog fuer die Dropdowns
 *   - sensor_dropdown_widgets[]      Setup-Screen-Dropdowns, ein Eintrag/Slot
 *   - load/save_*_nvs()              Dropdown-Auswahl <-> Flash
 *   - sensor_slots[]                 b) Hardware-Zuordnung: Typ + generierte
 *                                       Feldnamen je Slot im Weatherstation-Screen
 *   - apply_slot_configs()           Setup-Auswahl -> Weatherstation-Screen
 *   - disp_sensor_link_quality()     Batterie/Signal-Icon einfaerben
 *   - disp_sensor_values()           Messwerte eines Pakets anzeigen
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
  // https://de.wikipedia.org/wiki/Barometrische_H%C3%B6henformel

  // Konstanten
  float g = 9.80665;  // Schwerebeschleunigung in m / s^2
  float R = 287.05;   // Gaskonstante trockener Luft (= R/M)  in m^2/(s²K)
  float a = 0.0065;   // vertikaler Temperaturgradient
  float C_h = 0.12;   // Beiwert zur Berücksichtigung der mittleren Dampfdruckänderung K/hPa
  float T_0 = 273.15; // Celsius to Kelvin

  float E; // Dampfdruck des Wasserdampfanteils (in hPa)

  if (temperature < 9.1)
  {
    E = 5.6402 * (-0.0916 + exp(0.06 * temperature));
  }
  else
  {
    E = 18.2194 * (1.0463 + exp(-0.0666 * temperature));
  }

  // Luftdruck auf Meereshöhe berechnen
  float p = pressure * exp(altitude * g / (R * (temperature + T_0 + C_h * E + a * (altitude / 2))));

  return p;
}

/* a) Name+Icon-Katalog: jede Zeile ist eine Wahlmoeglichkeit im Setup-Screen-
 * Dropdown. Label = Anzeigename auf der Sensor-Karte, icon = zugehoeriges
 * Bild. Neuen Eintrag hinzufuegen = neue Zeile, fertig - wird automatisch
 * in allen 7 Dropdowns (Basis + 6 Sensoren) angeboten. */
typedef struct {
  const char *label;
  const lv_img_dsc_t *icon;
} sensor_icon_option_t;

static const sensor_icon_option_t sensor_icon_options[] = {
    {"Bad",          &img_sensor_bathroom},
    {"Balkon",       &img_sensor_balcony},
    {"Büro",         &img_sensor_office},
    {"Keller",       &img_sensor_cellar},
    {"Küche",        &img_sensor_kitchen},
    {"Schlafzimmer", &img_sensor_bedroom},
    {"Strahlung",    &img_sensor_radiation},
    {"Werkstatt",    &img_sensor_workshop},
    {"Wohnzimmer",   &img_sensor_home},
};
#define SENSOR_ICON_COUNT (sizeof(sensor_icon_options) / sizeof(sensor_icon_options[0]))

/* Ein Dropdown pro Slot legt Name UND Icon gemeinsam fest - der gewaehlte
 * Index zeigt direkt in sensor_icon_options[] (Label = Anzeigename, Icon =
 * Bild). Separate Namensfelder gibt es im Setup Screen nicht mehr. Index
 * 0-5 hier = Sensor 1-6 im UI = packet_header_t.sensor_nr im Funkpaket. */
static lv_obj_t **const sensor_dropdown_widgets[SENSOR_SLOT_COUNT] = {
    &objects.sensor_0_name, &objects.sensor_1_name, &objects.sensor_2_name,
    &objects.sensor_3_name, &objects.sensor_4_name, &objects.sensor_5_name,
};

static void populate_sensor_icon_dropdown(lv_obj_t *dropdown)
{
  lv_dropdown_clear_options(dropdown);
  for (size_t i = 0; i < SENSOR_ICON_COUNT; i++) {
    lv_dropdown_add_option(dropdown, sensor_icon_options[i].label, LV_DROPDOWN_POS_LAST);
  }
}

void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle)
{
  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    char key[20];

    populate_sensor_icon_dropdown(*sensor_dropdown_widgets[i]);
    snprintf(key, sizeof(key), "sensor%d_icon", i);
    uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, key, 0);
    lv_dropdown_set_selected(*sensor_dropdown_widgets[i], icon_idx < SENSOR_ICON_COUNT ? icon_idx : 0);
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

/* Basisstation (eigener SEN66-Sensor) - Name+Icon, kein Typ/keine Messwerte
 * ueber den Receiver, deshalb separat von den 6 Sensor-Slots gehalten. */
void load_basis_from_nvs(nvs_handle_t nvs_handle)
{
  populate_sensor_icon_dropdown(objects.basis_icon);
  uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, "icon_base", 0);
  lv_dropdown_set_selected(objects.basis_icon, icon_idx < SENSOR_ICON_COUNT ? icon_idx : 0);
}

void save_basis_to_nvs(nvs_handle_t nvs_handle)
{
  uint8_t icon_idx = lv_dropdown_get_selected(objects.basis_icon);
  put_uint8_to_nvs(nvs_handle, "icon_base", icon_idx);
}

/* b) Hardware-Zuordnung: jedes der 6 Sensor_X-Widgets (X=0..5, in EEZ Studio
 * direkt so benannt) hat dauerhaft einen fest verbauten Widget-Typ (aktuell:
 * Sensor_0/1/2/3=Sensor_Temp_Hum, Sensor_4=Sensor_Temp_Hum_Press,
 * Sensor_5=Sensor_Radiation; siehe screens.c fuer die tatsaechlich pro Slot
 * instanziierten create_user_widget_*()-Aufrufe). Diese Tabelle ist die
 * EINZIGE Stelle, die angepasst werden muss, wenn sich in EEZ Studio aendert,
 * welcher Widget-Typ in welchem Slot verbaut ist - Feldnamen einfach durch
 * die neuen ersetzen (siehe screens.h fuer die tatsaechlich generierten
 * objects.sensor_N__xxx-Namen). Solange die 6 Widgets in EEZ Studio ihren
 * Instanznamen (Sensor_0..Sensor_5) behalten, bleiben diese Feldnamen stabil
 * - unabhaengig davon, was sich sonst im Layout aendert.
 *
 * Die EEZ-Studio-Widget-Typen (Sensor_Temp_Hum, Sensor_Temp_Hum_Press, ...)
 * sind reine Layouts und bewusst NICHT nach Sensor-Hardware benannt - Temp+
 * Humidity sehen unabhaengig vom Sensor (SHT45, BME280, ...) gleich aus.
 * Jeder Widget-Typ hat eine eigene render()-Funktion, die den mitgegebenen
 * sensor_type_t selbst per switch auswertet, um das richtige Payload-Struct
 * zu casten - so kann z.B. dieselbe render_temp_hum() sowohl an einem
 * SHT45- als auch an einem BME280-Slot haengen, OHNE dass am Aufrufer
 * irgendwo Sensor-Hardware und Cast von Hand zusammenpassen muessen (genau
 * das ging vorher schief: Sensor 3 wurde auf SENSOR_TYPE_BME280 umgestellt,
 * die Render-Funktion castete aber weiter fix auf sht45_payload_t - falsche
 * Feldreihenfolge, Druck und Temperatur landeten vertauscht in den Labels).
 * sensor_type ist zusaetzlich weiterhin der Paket-Typ-Check auf Slot-Ebene
 * (kommt ein Paket rein, das nicht zum an diesem Slot verbauten Sensor
 * passt, wird es schon vor render() ignoriert). */
typedef struct {
  lv_obj_t **value1;   /* Temp (bme280/sht45) bzw. µSv/h (geiger) */
  lv_obj_t **value2;   /* Humidity (bme280/sht45), sonst NULL */
  lv_obj_t **value3;   /* Pressure (nur Temp_Hum_Press/-Compact), sonst NULL */
} sensor_values_t;

typedef void (*sensor_render_fn_t)(sensor_type_t type, const sensor_values_t *values, const void *payload);

typedef struct {
  sensor_type_t type;          /* erwarteter Pakettyp - fuer den Typ-Check */
  sensor_render_fn_t render;   /* Widget-Typ: weiss, welche Values befuellt werden */
  lv_obj_t **name;
  lv_obj_t **icon;
  lv_obj_t **battery;
  lv_obj_t **wifi;
  lv_obj_t **header;           /* Kopfzeile der Karte - wird bei "offline" rot eingefaerbt */
  sensor_values_t values;
} sensor_slot_t;

/* lv_label_set_text_fmt()/lv_snprintf() unterstuetzen hier keine
 * Float-Format-Specifier (CONFIG_LV_USE_FLOAT ist aus, nur
 * LV_USE_BUILTIN_SPRINTF) - "%.1f" etc. wuerden nur Muell/"f" anzeigen.
 * Deshalb wie im Rest von gui_sensors.c mit libc-sprintf in einen Puffer
 * formatieren und als fertigen String setzen. */

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
      return;   /* Sensor liefert keine Temp/Humidity-Werte */
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
      return;   /* Sensor liefert keinen Druckwert */
  }

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
  nvs_close(nvs_handle);
  float sea_level_pressure = calc_sea_level_pressure(pressure, temperature, (uint16_t)atol(height_c));

  char buf[16];
  sprintf(buf, "%.1f", temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.1f", humidity);
  lv_label_set_text(*v->value2, buf);
  sprintf(buf, "%.0f", sea_level_pressure);
  lv_label_set_text(*v->value3, buf);
}

/* Wie render_temp_hum_press(), nur Humidity ohne Nachkommastelle - im
 * Sensor_Temp_Hum_Press_Compact-Widget ist dafuer weniger Platz. Noch
 * keinem Slot zugewiesen (siehe sensor_slots[] unten). */
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
}

static const sensor_slot_t sensor_slots[SENSOR_SLOT_COUNT] = {
    // Schlafzimmer
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_0__name, &objects.sensor_0__icon, &objects.sensor_0__battery, &objects.sensor_0__wifi,
      &objects.sensor_0__header, { &objects.sensor_0__temp, &objects.sensor_0__humidity, NULL } },
    // Bad
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_1__name, &objects.sensor_1__icon, &objects.sensor_1__battery, &objects.sensor_1__wifi,
      &objects.sensor_1__header, { &objects.sensor_1__temp, &objects.sensor_1__humidity, NULL } },
    //Buero
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_2__name, &objects.sensor_2__icon, &objects.sensor_2__battery, &objects.sensor_2__wifi,
      &objects.sensor_2__header, { &objects.sensor_2__temp, &objects.sensor_2__humidity, NULL } },
    // Werkstatt
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_3__name, &objects.sensor_3__icon, &objects.sensor_3__battery, &objects.sensor_3__wifi,
      &objects.sensor_3__header, { &objects.sensor_3__temp, &objects.sensor_3__humidity, NULL } },
    // Balkon
    { SENSOR_TYPE_BME280, render_temp_hum_press, &objects.sensor_4__name, &objects.sensor_4__icon, &objects.sensor_4__battery, &objects.sensor_4__wifi,
      &objects.sensor_4__header, { &objects.sensor_4__temp, &objects.sensor_4__humidity, &objects.sensor_4__pressure } },
    // Geigerzaehler
    { SENSOR_TYPE_GEIGER, render_radiation,      &objects.sensor_5__name, &objects.sensor_5__icon, &objects.sensor_5__battery, &objects.sensor_5__wifi,
      &objects.sensor_5__header, { &objects.sensor_5__micro_sievert, NULL, NULL } },
};

/* Neuer Widget-Typ oder Slot wechselt auf einen bestehenden Typ (z.B. einen
 * Slot auf Sensor_Temp_Hum_Press_Compact umstellen): passende
 * render_xxx()-Funktion oben eintragen und die objects.sensor_N__xxx-Felder
 * unten anhand von screens.h aktualisieren (Achtung: EEZ Studio haengt bei
 * mehreren Widget-Instanzen mit gleichem Label pro Screen ggf. Suffixe wie
 * _1/_2 an die Feldnamen an - siehe sensor_4 oben). */

/**
 * @brief  Uebertraegt Name/Icon aus dem Setup Screen in die Basisstation
 *         (SEN66) und die 6 fest verdrahteten Sensor-Karten. Wird beim
 *         Klick auf "Starten" aufgerufen, bevor auf den Weatherstation-
 *         Screen gewechselt wird.
 */
void apply_slot_configs(void)
{
  uint8_t basis_icon_idx = lv_dropdown_get_selected(objects.basis_icon);
  if (basis_icon_idx >= SENSOR_ICON_COUNT) basis_icon_idx = 0;
  lv_label_set_text(objects.sen66__name, sensor_icon_options[basis_icon_idx].label);
  lv_image_set_src(objects.sen66__icon, sensor_icon_options[basis_icon_idx].icon);

  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    uint8_t icon_idx = lv_dropdown_get_selected(*sensor_dropdown_widgets[i]);
    if (icon_idx >= SENSOR_ICON_COUNT) icon_idx = 0;
    lv_label_set_text(*sensor_slots[i].name, sensor_icon_options[icon_idx].label);
    lv_image_set_src(*sensor_slots[i].icon, sensor_icon_options[icon_idx].icon);
  }
}

/**
 * @brief  Faerbt Batterie- und Signal-Icon einer Sensor-Karte nach Spannung/RSSI.
 *
 * @param  sensor_nr   0-5, wie im Packet-Header (packet_header_t.sensor_nr) -
 *                     identisch zum 0-basierten UI-Slot (Sensor 0-5)
 * @param  voltage_mv  Akkuspannung in mV, aus dem jeweiligen Payload
 * @param  rssi_dbm    Empfangsfeldstaerke in dBm, aus link_metadata_t.rssi
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
 * @brief  Schreibt die Messwerte eines empfangenen Pakets in die Sensor-Karte.
 *
 * @param  sensor_nr  0-5, siehe disp_sensor_link_quality()
 * @param  type       sensor_type_t des empfangenen Pakets
 * @param  payload    Rohes Payload (bme280_payload_t/sht45_payload_t/geiger_payload_t,
 *                     je nach type)
 *
 * @details Wenn der gemeldete Typ nicht zum in dieser Karte fest verbauten
 *          Typ passt (z.B. Sensor falsch konfiguriert/verdrahtet), wird das
 *          Paket ignoriert statt eine falsch beschriftete Karte zu befuellen.
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
 * @brief  Faerbt die Kopfzeile einer Sensor-Karte rot ein (Watchdog im
 *         Receiver hat lange kein Paket mehr von diesem Sensor bekommen)
 *         bzw. macht das wieder rueckgaengig, sobald der Sensor wieder sendet.
 *
 * @param  sensor_nr  0-5, siehe disp_sensor_link_quality()
 * @param  offline    true = rot einfaerben, false = Normalzustand wiederherstellen
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
