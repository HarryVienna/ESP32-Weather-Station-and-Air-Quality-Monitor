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
 * Sensoren - 6 Fernsensoren (LoRa/ESP-NOW)
 *
 * Siehe Kommentar am Kopf von gui_sensors.h fuer die Kurzfassung. Reihenfolge hier:
 *   - calc_sea_level_pressure()      Hilfsrechnung fuer BME280-Luftdruck
 *   - sensor_dropdown_widgets[]      Setup-Screen-Dropdowns, ein Eintrag/Slot
 *   - load/save_sensor_slots_*_nvs() Dropdown-Auswahl <-> Flash
 *   - render_xxx()                   Ein render() pro Widget-Typ (Temp_Hum/-Press/Radiation)
 *   - sensor_slots[]                 b) Hardware-Zuordnung: Typ + generierte
 *                                       Feldnamen je Slot im Weatherstation-Screen
 *   - apply_sensor_slot_configs()    Setup-Auswahl -> Weatherstation-Screen
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

/* Ein Dropdown pro Slot legt Name UND Icon gemeinsam fest - der gewaehlte
 * Index zeigt direkt in den Katalog aus gui_icon_catalog.h (Label =
 * Anzeigename, Icon = Bild). Separate Namensfelder gibt es im Setup Screen
 * nicht mehr. Index 0-5 hier = Sensor 1-6 im UI = packet_header_t.sensor_nr
 * im Funkpaket. */
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
  lv_obj_t **chart;    /* 24h-Verlaufschart (nur geiger), sonst NULL */
  lv_obj_t **quality;  /* Farbpanel (nur geiger), sonst NULL */
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
  sprintf(buf, "%.0f", humidity);
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

/* ============================================================================
 * Geigerzaehler - 24h-Verlaufschart (Slot 5, "Strahlung")
 *
 * Nutzt history_chart_t (siehe gui_history_chart.h) fuer das Peak-Bucketing: der
 * Sensor sendet 1x/Minute (siehe geiger_payload_t), das Chart-Widget ist
 * aber nur ca. 130px breit - history_chart_init() bestimmt daraus, wie viele
 * Minuten-Messwerte in einen Balken zusammengefasst werden (aufgerundet),
 * sodass die volle Chart-Breite immer >= 24h (RADIATION_HISTORY_SAMPLES_PER_DAY,
 * siehe gui_sensors.h) abdeckt statt nach point_count Minuten durchzuscrollen.
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

  /* THRESH_RADIATION ist in Centi-µSv/h definiert (siehe gui_color_scale.h), deshalb *100 */
  lv_obj_set_style_bg_color(*v->quality, level_color_asc(usvh * 100.0f, &THRESH_RADIATION), LV_PART_MAIN | LV_STATE_DEFAULT);

  history_chart_push(&radiation_chart, usvh);
}

/**
 * @brief  Initialisiert das 24h-Verlaufschart des Geigerzaehlers (Slot 5).
 *
 * @details Wie gui_sen66_init_charts() (siehe gui_sen66.h): einmalig beim
 *          Start aufrufen, bevor Pakete ueber disp_sensor_values()
 *          reinkommen.
 */
void gui_radiation_init_chart(void)
{
  lv_obj_update_layout(objects.weatherstation_screen);

  // 0-100 = 0.00-1.00 µSv/h, siehe THRESH_RADIATION
  history_chart_init(&radiation_chart, objects.sensor_5__chart_m_sv, 100,
                      lv_color_hex(0x616161), radiation_thresh_arr, RADIATION_HISTORY_SAMPLES_PER_DAY, 100.0f);
}

static const sensor_slot_t sensor_slots[SENSOR_SLOT_COUNT] = {
    // Schlafzimmer
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_0__name, &objects.sensor_0__icon, &objects.sensor_0__battery, &objects.sensor_0__wifi,
      &objects.sensor_0__header, { &objects.sensor_0__temp, &objects.sensor_0__humidity, NULL, NULL, NULL } },
    // Bad
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_1__name, &objects.sensor_1__icon, &objects.sensor_1__battery, &objects.sensor_1__wifi,
      &objects.sensor_1__header, { &objects.sensor_1__temp, &objects.sensor_1__humidity, NULL, NULL, NULL } },
    //Buero
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_2__name, &objects.sensor_2__icon, &objects.sensor_2__battery, &objects.sensor_2__wifi,
      &objects.sensor_2__header, { &objects.sensor_2__temp, &objects.sensor_2__humidity, NULL, NULL, NULL } },
    // Werkstatt
    { SENSOR_TYPE_SHT45,  render_temp_hum,       &objects.sensor_3__name, &objects.sensor_3__icon, &objects.sensor_3__battery, &objects.sensor_3__wifi,
      &objects.sensor_3__header, { &objects.sensor_3__temp, &objects.sensor_3__humidity, NULL, NULL, NULL } },
    // Balkon
    { SENSOR_TYPE_BME280, render_temp_hum_press, &objects.sensor_4__name, &objects.sensor_4__icon, &objects.sensor_4__battery, &objects.sensor_4__wifi,
      &objects.sensor_4__header, { &objects.sensor_4__temp, &objects.sensor_4__humidity, &objects.sensor_4__pressure, NULL, NULL } },
    // Geigerzaehler
    { SENSOR_TYPE_GEIGER, render_radiation,      &objects.sensor_5__name, &objects.sensor_5__icon, &objects.sensor_5__battery, &objects.sensor_5__wifi,
      &objects.sensor_5__header, { &objects.sensor_5__micro_sievert, NULL, NULL, &objects.sensor_5__chart_m_sv, &objects.sensor_5__m_sv } },
};

/* Neuer Widget-Typ oder Slot wechselt auf einen bestehenden Typ (z.B. einen
 * Slot auf Sensor_Temp_Hum_Press_Compact umstellen): passende
 * render_xxx()-Funktion oben eintragen und die objects.sensor_N__xxx-Felder
 * unten anhand von screens.h aktualisieren (Achtung: EEZ Studio haengt bei
 * mehreren Widget-Instanzen mit gleichem Label pro Screen ggf. Suffixe wie
 * _1/_2 an die Feldnamen an - siehe sensor_4 oben). */

/**
 * @brief  Uebertraegt Name/Icon aus dem Setup Screen in die 6 fest
 *         verdrahteten Sensor-Karten. Wird beim Klick auf "Starten" direkt
 *         neben apply_sen66_config() (siehe gui_sen66.h) aufgerufen, bevor
 *         auf den Weatherstation-Screen gewechselt wird.
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
