#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "esp_lvgl_port.h"

#include "nvs/preferences.h"

#include "task/clock_task.h"
#include "task/wifiscan_task.h"
#include "task/wificonnect_task.h"
#include "task/sensor_sen66_task.h"
#include "task/weather_task.h"
#include "task/brightness_task.h"
#include "task/wifistart_task.h"

#include "config/config.h"
#include "gui.h"
#include "../../common/packet_format.h"
#include "lvgl/lv_common.h"
#include "lvgl/lv_hourly_chart.h"
#include "lvgl/lv_daily_chart.h"
#include "lvgl/lv_screenshot.h"

static const char* TAG = "GUI";

/* ============================================================================
 * gui.c - Handgeschriebener GUI-Code (Gegenstueck zum EEZ-Studio-generierten
 * main/ui/screens.c). Alles hier ist normales C und wird NIE von EEZ Studio
 * ueberschrieben - im Gegensatz zu screens.c/.h, die bei jedem Export neu
 * erzeugt werden. Wenn du in EEZ Studio ein Widget umbenennst oder ein
 * neues anlegst, landen die Aenderungen dort; die Logik, die diese Widgets
 * mit Leben fuellt (NVS, Receiver-Daten, Wetter-API), steht hier.
 *
 * Inhalt dieser Datei, in dieser Reihenfolge:
 *   1. Farbskalen           - gemeinsame Gruen/Gelb/Rot-Bewertungslogik
 *   2. Weatherstation-Screen: Status - WLAN-Icon, Uhrzeit, Helligkeit
 *   3. SEN66                - eingebauter Luftqualitaetssensor der Basisstation
 *   4. Wettervorhersage     - Open-Meteo-Anzeige (aktuell/stuendlich/taeglich)
 *   5. Sensoren             - Basis + 6 Fernsensoren (LoRa/ESP-NOW)
 *   6. Chart-Infrastruktur  - Platzhalter-Charts aus EEZ Studio durch echte ersetzen
 *   7. Task-Start           - FreeRTOS-Tasks nach erfolgreichem Setup starten
 *   8. Setup Screen         - WLAN/Standort/Zeitzone/Sensor-Konfiguration
 *
 * -----------------------------------------------------------------------
 * Wie fuege ich einen Sensor hinzu / aendere einen bestehenden?
 * -----------------------------------------------------------------------
 * "Ein Sensor" besteht aus zwei unabhaengigen Dingen, beide im Abschnitt
 * "5. Sensoren" weiter unten:
 *
 * a) WAS man im Setup Screen auswaehlen kann (Name+Icon-Katalog):
 *    sensor_icon_options[]. Jeder Eintrag ist ein Name+Icon-Paar, das im
 *    Dropdown erscheint (z.B. {"Schlafzimmer", &img_sensor_bedroom}). Ein
 *    Dropdown legt beides zugleich fest - es gibt keine separaten
 *    Namensfelder mehr. Neues Icon zur Auswahl hinzufuegen = eine neue
 *    Zeile hier, sonst nichts.
 *
 * b) WELCHER Sensor-Typ (bme280/sht45/geiger) an WELCHER Stelle im
 *    Weatherstation-Screen fest verbaut ist: sensor_slots[]. Jeder der 6
 *    Sensor_X-Widgets (in EEZ Studio direkt so benannt, X=0..5) hat
 *    dauerhaft einen fest verbauten Widget-Typ. Diese Tabelle sagt
 *    disp_sensor_values()/disp_sensor_link_quality(), welche von EEZ
 *    generierten Feldnamen (objects.sensor_N__temp usw.) zu welchem Slot
 *    gehoeren. Aendert sich in EEZ Studio, welcher Typ in Slot X verbaut
 *    ist, wird NUR diese eine Tabellenzeile angepasst - der Rest der Datei
 *    bleibt unberuehrt.
 *
 * Der Slot-Index (0-5) ist ueberall derselbe: die UI-Reihenfolge
 * "Sensor 1..6" im Setup Screen, packet_header_t.sensor_nr im Funkpaket
 * (siehe common/packet_format.h) und der Index in sensor_slots[]/
 * sensor_dropdown_widgets[].
 * ============================================================================ */


/* ============================================================================
 * 1) Farbskalen - gemeinsame Schwellwert-Helfer
 *
 * Zwei Formen, je nachdem ob ein hoeherer Messwert besser oder schlechter
 * ist. Beide geben COLOR_GREEN..COLOR_RED zurueck (siehe config.h).
 * ============================================================================ */

/* Aufsteigend: t1 = bester (niedrigster) Wert. Fuer Messgroessen bei denen
 * ein hoeherer Wert schlechter ist (Feinstaub, VOC, NOx, CO2). */
typedef struct {
    float t1, t2, t3, t4;
} sen66_thresh_t;

static lv_color_t sen66_value_color(float val, const sen66_thresh_t *t)
{
    if      (val <= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val <= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val <= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val <= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}

/* Absteigend: t1 = bester (hoechster) Wert. Fuer Messgroessen bei denen ein
 * hoeherer Wert besser ist (Akkuspannung, RSSI). */
typedef struct {
  float t1, t2, t3, t4;
} level_thresh_t;

/* Einzelliger Li-Ion/LiPo-Akku: voll ~4.2V, leer/Abschaltung ~3.0V */
static const level_thresh_t thresh_battery_voltage = {4.0f, 3.8f, 3.6f, 3.4f};
/* RSSI in dBm, gemeinsame Skala fuer LoRa, ESP-NOW und WLAN */
static const level_thresh_t thresh_rssi_dbm = {-70.0f, -85.0f, -95.0f, -105.0f};

/* Fuer Batterie und Signalstaerke */
static lv_color_t level_color_desc(float val, const level_thresh_t *t)
{
    if      (val >= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val >= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val >= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val >= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}


/* ============================================================================
 * 2) Weatherstation-Screen: allgemeiner Status (WLAN, Uhrzeit, Helligkeit)
 * ============================================================================ */

/**
 * @brief  Faerbt das WLAN-Icon im Weatherstation-Screen nach Verbindungsstatus/RSSI.
 *
 * @param  status    true = verbunden, false = getrennt
 * @param  rssi_dbm  Empfangsfeldstaerke in dBm (nur relevant wenn status==true)
 */
void disp_wifi_status(bool status, int8_t rssi_dbm)
{
  lv_color_t color = status ? level_color_desc((float)rssi_dbm, &thresh_rssi_dbm)
                             : lv_color_hex(COLOR_RED);

  lvgl_port_lock(0);
  lv_obj_set_style_img_recolor(objects.current__wifi, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(objects.current__wifi, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lvgl_port_unlock();
}

void disp_date_time(char *date_time)
{
  lvgl_port_lock(0);
  lv_label_set_text(objects.current__date_time, date_time);
  lvgl_port_unlock();
}

void set_brightness(uint16_t brightness)
{
  display_set_brightness(brightness);
}


/* ============================================================================
 * 3) SEN66 - eingebauter Luftqualitaetssensor der Basisstation
 *
 * Name/Icon der SEN66-Karte werden wie bei den Fernsensoren im Setup Screen
 * konfiguriert (siehe Abschnitt 5, apply_slot_configs) - hier geht es nur
 * um die eigentlichen Messwerte (Temp/Feuchte/Feinstaub/VOC/NOx/CO2).
 * ============================================================================ */

static const sen66_thresh_t thresh_pm1   = {11.6f, 32.0f,  50.0f,  68.0f};
static const sen66_thresh_t thresh_pm2p5 = {13.0f, 35.0f,  55.0f,  75.0f};
static const sen66_thresh_t thresh_pm4   = {14.4f, 38.0f,  60.0f,  82.0f};
static const sen66_thresh_t thresh_pm10  = {20.0f, 50.0f,  80.0f, 110.0f};
static const sen66_thresh_t thresh_voc   = {50.0f, 150.0f, 250.0f, 400.0f};
static const sen66_thresh_t thresh_nox   = { 1.0f,  20.0f, 150.0f, 300.0f};
static const sen66_thresh_t thresh_co2   = {600.0f,1000.0f,1500.0f,1900.0f};

/* Threshold pointer arrays indexed by series id1, passed as user_data */
static const sen66_thresh_t *pm_thresh_arr[]  = {&thresh_pm2p5};
static const sen66_thresh_t *voc_thresh_arr[] = {&thresh_voc};
static const sen66_thresh_t *nox_thresh_arr[] = {&thresh_nox};
static const sen66_thresh_t *co2_thresh_arr[] = {&thresh_co2};

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

  lv_obj_set_style_bg_color(objects.sen66__pm1,   sen66_value_color(massConcentrationPm1p0,  &thresh_pm1),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm2p5, sen66_value_color(massConcentrationPm2p5,  &thresh_pm2p5), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm4,   sen66_value_color(massConcentrationPm4p0,  &thresh_pm4),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__pm10,  sen66_value_color(massConcentrationPm10p0, &thresh_pm10),  LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__voc,   sen66_value_color(vocIndex,                &thresh_voc),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__nox,   sen66_value_color(noxIndex,                &thresh_nox),   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(objects.sen66__co2,   sen66_value_color((float)co2,              &thresh_co2),   LV_PART_MAIN | LV_STATE_DEFAULT);

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

    const sen66_thresh_t *thresh = ((const sen66_thresh_t **)lv_event_get_user_data(e))[0];
    fill_dsc->color = sen66_value_color((float)val, thresh);
}


/* ============================================================================
 * 4) Wettervorhersage (Open-Meteo)
 * ============================================================================ */

#define NUM_ICONS 28

typedef struct
{
  const uint8_t icon;
  const lv_img_dsc_t *icon_image;
} icon_mapping_t;

icon_mapping_t icon_mapping_day[] = {
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

icon_mapping_t icon_mapping_night[] = {
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

  struct tm time_sunrise = daily_weather[0].sunrise;
  struct tm time_sunrset = daily_weather[0].sunset;
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


/* ============================================================================
 * 5) Sensoren - Basis (SEN66) + 6 Fernsensoren (LoRa/ESP-NOW)
 *
 * Siehe Kommentar am Dateianfang fuer die Kurzfassung. Reihenfolge hier:
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
float calc_sea_level_pressure(float pressure, float temperature, uint16_t altitude)
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

static void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle)
{
  for (int i = 0; i < SENSOR_SLOT_COUNT; i++) {
    char key[20];

    populate_sensor_icon_dropdown(*sensor_dropdown_widgets[i]);
    snprintf(key, sizeof(key), "sensor%d_icon", i);
    uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, key, 0);
    lv_dropdown_set_selected(*sensor_dropdown_widgets[i], icon_idx < SENSOR_ICON_COUNT ? icon_idx : 0);
  }
}

static void save_sensor_slots_to_nvs(nvs_handle_t nvs_handle)
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
static void load_basis_from_nvs(nvs_handle_t nvs_handle)
{
  populate_sensor_icon_dropdown(objects.basis_icon);
  uint8_t icon_idx = get_uint8_from_nvs(nvs_handle, "icon_base", 0);
  lv_dropdown_set_selected(objects.basis_icon, icon_idx < SENSOR_ICON_COUNT ? icon_idx : 0);
}

static void save_basis_to_nvs(nvs_handle_t nvs_handle)
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
 * Deshalb wie im Rest von gui.c mit libc-sprintf in einen Puffer
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
  if (type != SENSOR_TYPE_BME280) {
    return;   /* Druckwert gibt es nur vom BME280 */
  }
  const bme280_payload_t *d = (const bme280_payload_t *)payload;
  char buf[16];

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
  nvs_close(nvs_handle);
  float sea_level_pressure = calc_sea_level_pressure(d->pressure, d->temperature, (uint16_t)atol(height_c));

  sprintf(buf, "%.1f", d->temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.1f", d->humidity);
  lv_label_set_text(*v->value2, buf);
  sprintf(buf, "%.0f", sea_level_pressure);
  lv_label_set_text(*v->value3, buf);
}

/* Wie render_temp_hum_press(), nur Humidity ohne Nachkommastelle - im
 * Sensor_Temp_Hum_Press_Compact-Widget ist dafuer weniger Platz. Noch
 * keinem Slot zugewiesen (siehe sensor_slots[] unten). */
static void render_temp_hum_press_compact(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  if (type != SENSOR_TYPE_BME280) {
    return;
  }
  const bme280_payload_t *d = (const bme280_payload_t *)payload;
  char buf[16];

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
  nvs_close(nvs_handle);
  float sea_level_pressure = calc_sea_level_pressure(d->pressure, d->temperature, (uint16_t)atol(height_c));

  sprintf(buf, "%.1f", d->temperature);
  lv_label_set_text(*v->value1, buf);
  sprintf(buf, "%.0f", d->humidity);
  lv_label_set_text(*v->value2, buf);
  sprintf(buf, "%.0f", sea_level_pressure);
  lv_label_set_text(*v->value3, buf);
}

static void render_radiation(sensor_type_t type, const sensor_values_t *v, const void *payload)
{
  if (type != SENSOR_TYPE_GEIGER) {
    return;
  }
  const geiger_payload_t *d = (const geiger_payload_t *)payload;
  char buf[16];
  sprintf(buf, "%.2f", d->usvh);
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
    { SENSOR_TYPE_BME280, render_temp_hum,       &objects.sensor_3__name, &objects.sensor_3__icon, &objects.sensor_3__battery, &objects.sensor_3__wifi,
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
static void apply_slot_configs(void)
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

  lv_color_t battery_color = level_color_desc(voltage_mv / 1000.0f, &thresh_battery_voltage);
  lv_color_t signal_color = level_color_desc((float)rssi_dbm, &thresh_rssi_dbm);

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


/* ============================================================================
 * 6) Chart-Infrastruktur
 *
 * EEZ Studio hat kein Konzept fuer eigene native LVGL-Widget-Klassen, daher
 * legt es die Hourly/Daily-Charts nur als leere Platzhalter-Container an
 * (objects.hourly_chart / objects.daily_chart). init_charts() ersetzt diese
 * einmalig durch die echten lv_hourly_chart_t/lv_daily_chart_t-Widgets an
 * gleicher Position/Groesse und konfiguriert nebenbei die (von EEZ Studio
 * bereits als lv_chart angelegten) SEN66-Balkendiagramme. Wird einmal nach
 * create_screens() aufgerufen (siehe ui_init(), von main.c) - dadurch bleibt
 * screens.c komplett generiert, ein erneuter EEZ-Studio-Export verliert
 * diesen Schritt nie.
 * ============================================================================ */

void init_charts(void)
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

  // --- SEN66 charts (already lv_chart from EEZ Studio, just configure) ---

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


/* ============================================================================
 * 7) Task-Start
 *
 * Wird erst nach erfolgreichem WLAN-Connect aufgerufen (siehe
 * on_wifistart_done() im Setup-Screen-Abschnitt weiter unten) - vorher
 * ergeben Wetter-Abruf/Uhrzeit/SEN66-Messung keinen Sinn.
 * ============================================================================ */

void start_tasks()
{

  xTaskCreatePinnedToCore(
      clock_task,
      "Clock Task",
      4096,
      NULL,
      1,
      NULL,
      1);

  xTaskCreatePinnedToCore(
      weather_task,
      "Weather Task",
      16384,
      NULL,
      1,
      NULL,
      1);

  xTaskCreatePinnedToCore(
      sensor_sen66_task,
      "Sensor SEN66 Task",
      4096,
      NULL,
      1,
      NULL,
      1);

  // xTaskCreatePinnedToCore(
  //     brightness_task,
  //     "Brightness Task",
  //     4096,
  //     NULL,
  //     1,
  //     NULL,
  //     1);


}


/* ============================================================================
 * 8) Setup Screen
 * ============================================================================ */

const char *regionNames[] = {
    "Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific"};

const char *cityData[][3] = {
    {"Africa", "(GMT) Casablanca", "WET0WEST,M3.5.0,M10.5.0/3"},
    {"Africa", "(GMT +01:00) West Central Africa", "WAT-1"},
    {"Africa", "(GMT +02:00) Harare, Pretoria", "CAT-2"},
    {"Africa", "(GMT +02:00) Windhoek", "WAT-1WAST,M9.1.0,M4.1.0"},
    {"Africa", "(GMT +02:00) Cairo", "EET-2"},
    {"Africa", "(GMT +03:00) Nairobi", "EAT-3"},

    {"America", "(GMT -03:00) Buenos Aires", "ART3"},
    {"America", "(GMT -03:00) Brasilia", "BRT3BRST,M10.3.0/0,M2.3.0/0"},
    {"America", "(GMT -03:00) Greenland", "WGT3WGST,M3.5.0/-2,M10.5.0/-1"},
    {"America", "(GMT -03:00) Montevideo", "UYT3"},
    {"America", "(GMT -03:30) Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -03:00) Cayenne, Fortaleza", "GFT3"},
    {"America", "(GMT -04:00) Atlantic Time (Canada)", "AST4ADT,M3.2.0,M11.1.0"},
    {"America", "(GMT -04:00) Cuiaba", "AMT4AMST,M10.3.0/0,M2.3.0/0"},
    {"America", "(GMT -04:00) Santiago", "CLT3"},
    {"America", "(GMT -04:00) Asuncion", "PYT4PYST,M10.1.0/0,M3.4.0/0"},
    {"America", "(GMT -04:00) Georgetown, La Paz, Manaus, San Juan", "BOT4"},
    {"America", "(GMT -04:30) Caracas", "VET4:30"},
    {"America", "(GMT -05:00) Eastern Time (US & Canada)", "EST5EDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -05:00) Bogota, Lima, Quito", "COT5"},
    {"America", "(GMT -05:00) Indiana (East)", "EST5EDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -06:00) Saskatchewan", "CST6"},
    {"America", "(GMT -06:00) Central America", "CST6"},
    {"America", "(GMT -06:00) Guadalajara, Mexico City, Monterrey", "CST6CDT,M4.1.0,M10.5.0"},
    {"America", "(GMT -06:00) Central Time (US & Canada)", "CST6CDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -07:00) Chihuahua, La Paz, Mazatlan", "MST7MDT,M4.1.0,M10.5.0"},
    {"America", "(GMT -07:00) Mountain Time (US & Canada)", "MST7MDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -07:00) Arizona", "MST7"},
    {"America", "(GMT -08:00) Baja California", "PST8PDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -08:00) Pacific Time (US & Canada)", "PST8PDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -09:00) Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America", "(GMT -10:00) Hawaii-Aleutian", "HST10HDT,M3.2.0,M11.1.0"},

    {"Antarctica", "(GMT +12:00) McMurdo", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Antarctica", "(GMT +11:00) Macquarie", "MIST-11"},
    {"Antarctica", "(GMT +10:00) DumontDUrville", "DDUT-10"},
    {"Antarctica", "(GMT +08:00) Casey", "AWST-8"},
    {"Antarctica", "(GMT +07:00) Davis", "DAVT-7"},
    {"Antarctica", "(GMT +06:00) Vostok", "VOST-6"},
    {"Antarctica", "(GMT +05:00) Mawson", "MAWT-5"},
    {"Antarctica", "(GMT +03:00) Syowa", "SYOT-3"},
    {"Antarctica", "(GMT -04:00) Palmer", "CLT3"},
    {"Antarctica", "(GMT -04:00) Rothera", "ROTT3"},

    {"Arctic", "(GMT +01:00) Longyearbyen", "CET-1CEST,M3.5.0,M10.5.0/3"},

    {"Asia", "(GMT +12:00) Petropavlovsk-Kamchatsky", "PETT-12"},
    {"Asia", "(GMT +11:00) Magadan", "MAGT-1"},
    {"Asia", "(GMT +10:00) Vladivostok", "VLAT-10"},
    {"Asia", "(GMT +09:00) Yakutsk", "YAKT-9"},
    {"Asia", "(GMT +09:00) Osaka, Sapporo, Tokyo", "JST-9"},
    {"Asia", "(GMT +09:00) Seoul", "KST-9"},
    {"Asia", "(GMT +08:00) Kuala Lumpur, Singapore", "SGT-8"},
    {"Asia", "(GMT +08:00) Ulaanbaatar", "ULAT-8ULAST,M3.5.6,M9.5.6/0"},
    {"Asia", "(GMT +08:00) Taipei", "CST-8"},
    {"Asia", "(GMT +08:00) Irkutsk", "IRKT-8"},
    {"Asia", "(GMT +08:00) Beijing, Chongqing, Hong Kong, Urumqi", "HKT-8"},
    {"Asia", "(GMT +07:00) Bangkok, Hanoi, Jakarta", "=WIB-7"},
    {"Asia", "(GMT +07:00) Krasnoyarsk", "KRAT-7"},
    {"Asia", "(GMT +06:30) Yangon (Rangoon)", "UNK-6:30"},
    {"Asia", "(GMT +06:00) Novosibirsk", "NOVT-6"},
    {"Asia", "(GMT +06:00) Astana", "(GMT-4"},
    {"Asia", "(GMT +06:00) Dhaka", "BDT-6"},
    {"Asia", "(GMT +05:45) Kathmandu", "NPT-5:45"},
    {"Asia", "(GMT +05:30) Sri Jayawardenepura", "IST-5:30"},
    {"Asia", "(GMT +05:30) Chennai, Kolkata, Mumbai, New Delhi", "IST-5:30"},
    {"Asia", "(GMT +05:00) Tashkent", "UZT-5"},
    {"Asia", "(GMT +05:00) Islamabad, Karachi", "PKT-5"},
    {"Asia", "(GMT +05:00) Ekaterinburg", "YEKT-5"},
    {"Asia", "(GMT +04:00) Tbilisi", "GET-4"},
    {"Asia", "(GMT +04:00) Yerevan", "AMT-4"},
    {"Asia", "(GMT +04:00) Baku", "AZT-4AZST,M3.5.0/4,M10.5.0/5"},
    {"Asia", "(GMT +04:00) Abu Dhabi, Muscat", "GST-4"},
    {"Asia", "(GMT +04:30) Kabul", "AFT-4:30"},
    {"Asia", "(GMT +03:30) Tehran", "IRST-3:30IRDT,80/0,264/0"},
    {"Asia", "(GMT +03:00) Baghdad", "AST-3"},
    {"Asia", "(GMT +03:00) Kuwait, Riyadh", "AST-3"},
    {"Asia", "(GMT +02:00) Damascus", "EET-2EEST,M3.5.5/0,M10.5.5/0"},
    {"Asia", "(GMT +02:00) Beirut", "EET-2EEST,M3.5.0/0,M10.5.0/0"},
    {"Asia", "(GMT +02:00) Amman", "EET-2EEST,M3.5.4/24,M10.5.5/1"},
    {"Asia", "(GMT +02:00) Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},

    {"Atlantic", "(GMT) Monrovia, Reykjavik", "GMT0"},
    {"Atlantic", "(GMT -01:00) Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1"},
    {"Atlantic", "(GMT -01:00) Cape Verde Is.", "CVT1"},

    {"Australia", "(GMT +10:00) Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia", "(GMT +10:00) Brisbane", "AEST-10"},
    {"Australia", "(GMT +10:00) Canberra, Melbourne, Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia", "(GMT +09:30) Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia", "(GMT +09:30) Darwin", "ACST-9:30"},
    {"Australia", "(GMT +08:00) Perth", "AWST-8"},

    {"Europe", "(GMT) Dublin, Edinburgh, Lisbon, London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe", "(GMT +01:00) Belgrade, Bratislava, Budapest, Ljubljana, Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe", "(GMT +01:00) Sarajevo, Skopje, Warsaw, Zagreb", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe", "(GMT +01:00) Brussels, Copenhagen, Madrid, Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe", "(GMT +01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe", "(GMT +02:00) Chisinau", "EET-2EEST,M3.5.0,M10.5.0/3"},
    {"Europe", "(GMT +02:00) Helsinki, Kyiv, Riga, Sofia, Tallinn, Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe", "(GMT +02:00) Athens, Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe", "(GMT +03:00) Minsk", "MSK-3"},
    {"Europe", "(GMT +03:00) Moscow, St. Petersburg, Volgograd", "MSK-3"},
    {"Europe", "(GMT +03:00) Istanbul", "EET-3"},

    {"Indian", "(GMT +04:00) Port Louis", "MUT-4"},
    {"Indian", "(GMT +07:00) Christmas Island", "CXT-7"},

    {"Pacific", "(GMT +13:00) Nuku'alofa", "TOT-13"},
    {"Pacific", "(GMT +12:00) Fiji, Marshall Is.", "FJT-12FJST,M11.1.0,M1.3.0/3"},
    {"Pacific", "(GMT +12:00) Auckland, Wellington", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific", "(GMT +11:00) Solomon Is., New Caledonia", "SBT-11"},
    {"Pacific", "(GMT +10:00) Guam, Port Moresby", "PGT-10"},
    {"Pacific", "(GMT -11:00) Samoa", "WSST-13WSDT,M9.5.0/3,M4.1.0/4"},
    {"Pacific", "(GMT -10:00) Hawaii", "HST10"},
    {"Pacific", "(GMT -05:00) Easter Island", "EAST5"}};

void set_cities(const char *region)
{
  lv_dropdown_clear_options(objects.dropdown_city);
  for (size_t i = 0; i < sizeof(cityData) / sizeof(cityData[0]); i++)
  {
    if (strcmp(cityData[i][0], region) == 0)
    {
      // Found a matching region, split city names and add them to the dropdown
      const char *cities = cityData[i][1];
      lv_dropdown_add_option(objects.dropdown_city, cities, LV_DROPDOWN_POS_LAST);
    }
  }
}

void disp_wifi_networks(char* allNetworks)
{
  lv_dropdown_clear_options(objects.dropdown_networks);
  lv_dropdown_set_options(objects.dropdown_networks, allNetworks);
}

void disp_show_setup_spinner(bool show)
{
  if (show)
  {
    lv_obj_clear_flag(objects.panel_setup_spinner, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    lv_obj_add_flag(objects.panel_setup_spinner, LV_OBJ_FLAG_HIDDEN);
  }
}

void disp_connect_status(bool is_connected)
{
  if (is_connected)
  {
    lv_obj_set_style_bg_color(objects.text_area_password, lv_color_hex(COLOR_LIGHTGREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(objects.text_area_password, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  else
  {
    lv_obj_set_style_bg_color(objects.text_area_password, lv_color_hex(COLOR_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(objects.text_area_password, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

void action_event_setup_screen_loaded(lv_event_t *e)
{
  // EEZ Studio doesn't mark this panel hidden by default, unlike the keyboards
  disp_show_setup_spinner(false);

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);

  char* ssid = get_string_from_nvs(nvs_handle, "ssid", "");
  char* password = get_string_from_nvs(nvs_handle, "password", "");
  char* appid = get_string_from_nvs(nvs_handle, "appid", "");
  char* latitude = get_string_from_nvs(nvs_handle, "latitude", "");
  char* longitude =get_string_from_nvs(nvs_handle, "longitude", "");
  char* height = get_string_from_nvs(nvs_handle, "height", "");
  uint8_t region_id = get_uint8_from_nvs(nvs_handle, "region", 0);
  uint8_t city_id = get_uint8_from_nvs(nvs_handle, "city", 0);
  load_basis_from_nvs(nvs_handle);
  load_sensor_slots_from_nvs(nvs_handle);

  nvs_close(nvs_handle);

  if (strcmp(ssid, "") != 0)
  {
    lv_dropdown_clear_options(objects.dropdown_networks);
    lv_dropdown_add_option(objects.dropdown_networks, ssid, LV_DROPDOWN_POS_LAST);
  }
  lv_textarea_set_text(objects.text_area_password, password);
  lv_textarea_set_text(objects.text_area_app_id, appid);

  lv_textarea_set_text(objects.text_area_latitude, latitude);
  lv_textarea_set_text(objects.text_area_longitude, longitude);
  lv_textarea_set_text(objects.text_area_hoehe, height);

  // fill the region names
  for (size_t i = 0; i < sizeof(regionNames) / sizeof(regionNames[0]); i++)
  {
    lv_dropdown_add_option(objects.dropdown_region, regionNames[i], LV_DROPDOWN_POS_LAST);
  }
  lv_dropdown_set_selected(objects.dropdown_region, region_id); // set the selected region id
  // get the region name
  char region[64];
  lv_dropdown_get_selected_str(objects.dropdown_region, region, sizeof(region));
  // fill the city list
  set_cities(region);
  // set the selected city id
  lv_dropdown_set_selected(objects.dropdown_city, city_id);
}

static void on_wifiscan_done(char *networks)
{
  lvgl_port_lock(0);
  disp_wifi_networks(networks);
  disp_show_setup_spinner(false);
  lvgl_port_unlock();
}

void action_event_wifi_scan(lv_event_t *e)
{
  disp_show_setup_spinner(true);
  wifiscan_start(on_wifiscan_done);
}

static void on_wificonnect_done(bool connected)
{
  lvgl_port_lock(0);
  disp_connect_status(connected);
  disp_show_setup_spinner(false);
  lvgl_port_unlock();
}

void action_event_wifi_connect(lv_event_t *e)
{
  char network[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, network, sizeof(network));
  const char *password = lv_textarea_get_text(objects.text_area_password);

  disp_show_setup_spinner(true);
  wificonnect_start(network, password, on_wificonnect_done);
}

void action_event_timezone_value_changed(lv_event_t *e)
{
  int selectedRegion = lv_dropdown_get_selected(objects.dropdown_region);
  ESP_LOGI(TAG,"selectedRegion: %d", selectedRegion);

  char region[64];
  lv_dropdown_get_selected_str(objects.dropdown_region, region, sizeof(region));
  ESP_LOGI(TAG,"region: %s", region);

  set_cities(region);

  lv_dropdown_set_selected(objects.dropdown_city, 0);
}



static void on_wifistart_done(void)
{
  start_tasks();

  lvgl_port_lock(0);
  disp_show_setup_spinner(false);
  loadScreen(SCREEN_ID_WEATHERSTATION_SCREEN);
  lvgl_port_unlock();
}

void action_event_weatherstation_start(lv_event_t *e)
{
  // Store preferences
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);

  char ssid[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, ssid, sizeof(ssid));
  put_string_to_nvs(nvs_handle, "ssid", ssid);

  const char* password = lv_textarea_get_text(objects.text_area_password);
  put_string_to_nvs(nvs_handle, "password", password);

  const char* appid = lv_textarea_get_text(objects.text_area_app_id);
  put_string_to_nvs(nvs_handle, "appid", appid);

  const char* latitude = lv_textarea_get_text(objects.text_area_latitude);
  put_string_to_nvs(nvs_handle, "latitude", latitude);

  const char* longitude = lv_textarea_get_text(objects.text_area_longitude);
  put_string_to_nvs(nvs_handle, "longitude", longitude);

  const char* height = lv_textarea_get_text(objects.text_area_hoehe);
  put_string_to_nvs(nvs_handle, "height", height);

  uint8_t region_id = lv_dropdown_get_selected(objects.dropdown_region);
  put_uint8_to_nvs(nvs_handle, "region", region_id);

  uint8_t city_id = lv_dropdown_get_selected(objects.dropdown_city);
  put_uint8_to_nvs(nvs_handle, "city", city_id);

  const char* tz = NULL;
  const char *region = regionNames[region_id];
  for (size_t i = 0; i < sizeof(cityData) / sizeof(cityData[0]); i++)
  {
    if (strcmp(cityData[i][0], region) == 0)
    {
      tz = cityData[i + city_id][2];
      break;
    }
  }
  put_string_to_nvs(nvs_handle, "tz", tz);

  save_basis_to_nvs(nvs_handle);
  save_sensor_slots_to_nvs(nvs_handle);

  nvs_close(nvs_handle);

  apply_slot_configs();

  disp_show_setup_spinner(true);
  wifistart_start(on_wifistart_done);
}

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
