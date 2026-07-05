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


static lv_chart_series_t *ser_pm2p5 = NULL;
static lv_chart_series_t *ser_voc   = NULL;
static lv_chart_series_t *ser_nox   = NULL;
static lv_chart_series_t *ser_co2   = NULL;

typedef struct {
    float t1, t2, t3, t4;
} sen66_thresh_t;

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

/* Wie sen66_thresh_t, aber absteigend: t1 = bester (hoechster) Wert. Fuer
 * Metriken bei denen ein hoeherer Wert besser ist (Spannung, RSSI). */
typedef struct {
  float t1, t2, t3, t4;
} level_thresh_t;

/* Einzelliger Li-Ion/LiPo-Akku: voll ~4.2V, leer/Abschaltung ~3.0V */
static const level_thresh_t thresh_battery_voltage = {4.0f, 3.8f, 3.6f, 3.4f};
/* RSSI in dBm, gemeinsame Skala fuer LoRa, ESP-NOW und WLAN */
static const level_thresh_t thresh_rssi_dbm = {-70.0f, -85.0f, -95.0f, -105.0f};

static lv_color_t level_color_desc(float val, const level_thresh_t *t)
{
    if      (val >= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val >= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val >= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val >= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}

static lv_color_t sen66_value_color(float val, const sen66_thresh_t *t)
{
    if      (val <= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val <= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val <= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val <= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}

#define NUM_ICONS 28

typedef struct
{
  const uint8_t icon;
  const lv_img_dsc_t *icon_image;
} icon_mapping_t;

icon_mapping_t icon_mapping_day[] = {
      {0, &img_0d},
      {1, &img_1d},
      {2, &img_2},
      {3, &img_3},
      {45, &img_45},
      {48, &img_48},
      {51, &img_51},
      {53, &img_53},
      {55, &img_55},
      {56, &img_56},
      {57, &img_57},
      {61, &img_61},
      {63, &img_63},
      {65, &img_65},
      {66, &img_66},
      {67, &img_67},
      {71, &img_71},
      {73, &img_73},
      {75, &img_75},
      {77, &img_77},
      {80, &img_80},
      {81, &img_81},
      {82, &img_82},
      {85, &img_85},
      {86, &img_86},
      {95, &img_95},
      {96, &img_96},
      {99, &img_99}};

icon_mapping_t icon_mapping_night[] = {
      {0, &img_0n},
      {1, &img_1n},
      {2, &img_2},
      {3, &img_3},
      {45, &img_45},
      {48, &img_48},
      {51, &img_51},
      {53, &img_53},
      {55, &img_55},
      {56, &img_56},
      {57, &img_57},
      {61, &img_61},
      {63, &img_63},
      {65, &img_65},
      {66, &img_66},
      {67, &img_67},
      {71, &img_71},
      {73, &img_73},
      {75, &img_75},
      {77, &img_77},
      {80, &img_80},
      {81, &img_81},
      {82, &img_82},
      {85, &img_85},
      {86, &img_86},
      {95, &img_95},
      {96, &img_96},
      {99, &img_99}};      

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

// -------- Weatherstation Screen --------

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

  lv_obj_set_style_img_recolor(objects.wifi, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(objects.wifi, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void disp_date_time(char *date_time)
{
  lv_label_set_text(objects.date_time, date_time);
}

void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2)
{

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
}

void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2)
{

    lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm2p5, (int32_t)pm2p5);
    lv_chart_set_next_value(objects.sen66__chart_voc, ser_voc,  (int32_t)voc);
    lv_chart_set_next_value(objects.sen66__chart_nox, ser_nox,  (int32_t)nox);
    lv_chart_set_next_value(objects.sen66__chart_co2, ser_co2,  (float)co2);
}

void disp_weather(current_weather_data_t *current_weather, hourly_weather_data_t *hourly_weather, daily_weather_data_t *daily_weather) {

  // Current data
  char temp[8];
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
      lv_img_set_src(objects.weather_icon, icon_mapping[i].icon_image);
      break;
    }
  }

  sprintf(temp, "%.1f", current_weather->temperature_2m);
  lv_label_set_text(objects.temp_current, temp);

  sprintf(clouds, "%d", current_weather->cloud_cover);
  lv_label_set_text(objects.clouds_current, clouds);

  sprintf(uv_index, "%d", (int) round(current_weather->uv_index));
  lv_label_set_text(objects.uv_current, uv_index);

  sprintf(wind_speed, "%.1f", current_weather->wind_speed_10m);
  lv_label_set_text(objects.wind_speed_current, wind_speed);

  sprintf(wind_gust, "%.1f", current_weather->wind_gusts_10m);
  lv_label_set_text(objects.wind_gust_current, wind_gust);

  lv_img_set_angle(objects.wind_direction_current_icon, current_weather->wind_direction_10m * 10);

  struct tm time_sunrise = daily_weather[0].sunrise;
  struct tm time_sunrset = daily_weather[0].sunset;
  strftime(str_sunrise, sizeof(str_sunrise), "%H:%M", &time_sunrise);
  lv_label_set_text(objects.sunrise_current, str_sunrise);
  strftime(str_sunset, sizeof(str_sunset), "%H:%M", &time_sunrset);
  lv_label_set_text(objects.sunset_current, str_sunset);

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
}


void set_brightness(uint16_t brightness)
{
  display_set_brightness(brightness);
}

// -------- Setup Screen --------

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

#define SENSOR_SLOT_COUNT 6

typedef struct {
  const char *label;
  const lv_img_dsc_t *icon;
} sensor_icon_option_t;

static const sensor_icon_option_t sensor_icon_options[] = {
    {"Bad",          &img_sensor_bathroom},
    {"Balkon",       &img_sensor_balcony},
    {"Büro",         &img_sensor_office},
    {"Keller",       &img_sensor_cellar},
    {"Schlafzimmer", &img_sensor_bedroom},
    {"Strahlung",    &img_sensor_radiation},
    {"Werkstatt",    &img_sensor_workshop},
    {"Zuhause",      &img_sensor_home},
};
#define SENSOR_ICON_COUNT (sizeof(sensor_icon_options) / sizeof(sensor_icon_options[0]))

/* Ein Dropdown pro Slot legt Name UND Icon gemeinsam fest - der gewaehlte
 * Index zeigt direkt in sensor_icon_options[] (Label = Anzeigename, Icon =
 * Bild). Separate Namensfelder gibt es im Setup Screen nicht mehr. */
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

/* ============================================================================
 * Feste Sensor-Karten in Container_Sensoren (Weatherstation-Screen)
 *
 * Jeder der 6 Sensor_X-Container hat in EEZ Studio dauerhaft einen fest
 * verbauten Widget-Typ (aktuell: Sensor_0=bme280, Sensor_1-4=sht45,
 * Sensor_5=geiger). Diese Tabelle ist die EINZIGE Stelle, die angepasst
 * werden muss, wenn sich in EEZ Studio aendert, welcher Typ in welchem
 * Slot verbaut ist - Feldnamen einfach durch die neuen ersetzen.
 * ============================================================================ */

typedef struct {
  sensor_type_t type;
  lv_obj_t **name;
  lv_obj_t **icon;
  lv_obj_t **battery;
  lv_obj_t **wifi;
  lv_obj_t **value1;   /* Temp (bme280/sht45) bzw. µSv/h (geiger) */
  lv_obj_t **value2;   /* Humidity (bme280/sht45) bzw. CPM (geiger) */
  lv_obj_t **value3;   /* Pressure (nur bme280), sonst NULL */
} sensor_slot_t;

static const sensor_slot_t sensor_slots[SENSOR_SLOT_COUNT] = {
    { SENSOR_TYPE_BME280, &objects.obj1__name,      &objects.obj1__icon, &objects.obj1__battery, &objects.obj1__wifi,
      &objects.obj1__temp, &objects.obj1__humidity, &objects.obj1__pressure },
    { SENSOR_TYPE_SHT45,  &objects.obj2__name,      &objects.obj2__icon, &objects.obj2__battery, &objects.obj2__wifi,
      &objects.obj2__temp, &objects.obj2__humidity, NULL },
    { SENSOR_TYPE_SHT45,  &objects.obj3__name,      &objects.obj3__icon, &objects.obj3__battery, &objects.obj3__wifi,
      &objects.obj3__temp, &objects.obj3__humidity, NULL },
    { SENSOR_TYPE_SHT45,  &objects.obj4__name,      &objects.obj4__icon, &objects.obj4__battery, &objects.obj4__wifi,
      &objects.obj4__temp, &objects.obj4__humidity, NULL },
    { SENSOR_TYPE_SHT45,  &objects.obj5__name,      &objects.obj5__icon, &objects.obj5__battery, &objects.obj5__wifi,
      &objects.obj5__temp, &objects.obj5__humidity, NULL },
    { SENSOR_TYPE_GEIGER, &objects.obj6__name,      &objects.obj6__icon, &objects.obj6__battery, &objects.obj6__wifi,
      &objects.obj6__micro_sievert, &objects.obj6__cpm, NULL },
};

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
  lv_obj_set_style_img_recolor(*slot->battery, battery_color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(*slot->battery, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_color_t signal_color = level_color_desc((float)rssi_dbm, &thresh_rssi_dbm);
  lv_obj_set_style_img_recolor(*slot->wifi, signal_color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(*slot->wifi, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
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

  /* lv_label_set_text_fmt()/lv_snprintf() unterstuetzen hier keine
   * Float-Format-Specifier (CONFIG_LV_USE_FLOAT ist aus, nur
   * LV_USE_BUILTIN_SPRINTF) - "%.1f" etc. wuerden nur Muell/"f" anzeigen.
   * Deshalb wie im Rest von gui.c mit libc-sprintf in einen Puffer
   * formatieren und als fertigen String setzen. */
  char buf[16];

  switch (type) {
    case SENSOR_TYPE_BME280: {
      const bme280_payload_t *d = (const bme280_payload_t *)payload;

      nvs_handle_t nvs_handle;
      nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
      const char *height_c = get_string_from_nvs(nvs_handle, "height", "0");
      nvs_close(nvs_handle);
      float sea_level_pressure = calc_sea_level_pressure(d->pressure, d->temperature, (uint16_t)atol(height_c));

      sprintf(buf, "%.1f", d->temperature);
      lv_label_set_text(*slot->value1, buf);
      sprintf(buf, "%.1f", d->humidity);
      lv_label_set_text(*slot->value2, buf);
      sprintf(buf, "%.0f", sea_level_pressure);
      lv_label_set_text(*slot->value3, buf);
      break;
    }
    case SENSOR_TYPE_SHT45: {
      const sht45_payload_t *d = (const sht45_payload_t *)payload;
      sprintf(buf, "%.1f", d->temperature);
      lv_label_set_text(*slot->value1, buf);
      sprintf(buf, "%.1f", d->humidity);
      lv_label_set_text(*slot->value2, buf);
      break;
    }
    case SENSOR_TYPE_GEIGER: {
      const geiger_payload_t *d = (const geiger_payload_t *)payload;
      sprintf(buf, "%.3f", d->usvh);
      lv_label_set_text(*slot->value1, buf);
      sprintf(buf, "%.1f", d->cpm);
      lv_label_set_text(*slot->value2, buf);
      break;
    }
    default:
      break;
  }
}

/**
 * @brief     Swap the EEZ Studio chart placeholders for the real chart widgets
 *
 * @details   EEZ Studio has no concept of a custom native LVGL widget class, so
 *            the hourly/daily charts are laid out there as plain containers
 *            (objects.hourly_chart / objects.daily_chart). Call this once after
 *            create_screens() (see ui_init(), called from main.c) to replace
 *            each placeholder with the real lv_hourly_chart/lv_daily_chart
 *            widget at the same position/size, before anything else touches
 *            objects.hourly_chart/daily_chart. This keeps screens.c entirely
 *            generated - re-running the EEZ Studio build never loses this.
 */


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
  //     lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm10,  (int32_t)(20 + 15 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm4,   (int32_t)(14 + 10 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm2p5, (int32_t)( 9 +  7 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_pm, ser_pm1,   (int32_t)( 5 +  4 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_voc, ser_voc,  (int32_t)(120 + 60 * c));
  //     lv_chart_set_next_value(objects.sen66__chart_nox, ser_nox,  (int32_t)(  5 + 200 * s));
  //     lv_chart_set_next_value(objects.sen66__chart_co2, ser_co2,  (int32_t)(800 + 200 * c));
  // }
}

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

// -------- LVGL Events --------

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