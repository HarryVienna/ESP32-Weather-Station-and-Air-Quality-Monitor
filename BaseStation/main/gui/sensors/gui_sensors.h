#ifndef GUI_SENSORS_H
#define GUI_SENSORS_H

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"

#include "nvs/preferences.h"
#include "config/config.h"
#include "../../../common/packet_format.h"

/* Sensoren - Basis (SEN66) + 6 Fernsensoren (LoRa/ESP-NOW), plus die
 * gemeinsamen Wert->Farbe-Funktionen (frueher gui_color.c/h) - alles hier
 * drin, weil praktisch nur von gui_sensors.c selbst gebraucht; einzige
 * Ausnahme ist level_color_desc(), das auch gui_status.c fuer das
 * WLAN-Icon braucht.
 *
 * -----------------------------------------------------------------------
 * Wie fuege ich einen Sensor hinzu / aendere einen bestehenden?
 * -----------------------------------------------------------------------
 * "Ein Sensor" besteht aus zwei unabhaengigen Dingen, beide in gui_sensors.c:
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
 * sensor_dropdown_widgets[]. */

/* Anzahl der fest verdrahteten Sensor-Karten im Weatherstation-Screen (siehe
 * sensor_slots[] in gui_sensors.c). sensor_nr aus packet_header_t muss <
 * diesem Wert sein, um einer Karte zugeordnet zu werden - wird auch vom
 * Receiver-Watchdog genutzt, um die Anzahl der zu ueberwachenden Slots zu
 * kennen. */
#define SENSOR_SLOT_COUNT 6

/* Absteigend: t1 = bester (hoechster) Wert. Fuer Messgroessen bei denen ein
 * hoeherer Wert besser ist (Akkuspannung, RSSI). Einzige der beiden
 * Farbskalen-Funktionen, die auch ausserhalb von gui_sensors.c gebraucht
 * wird (gui_status.c, WLAN-Icon) - sen66_value_color() bleibt dagegen
 * static in gui_sensors.c, da nur intern fuer SEN66-Werte benutzt. */
lv_color_t level_color_desc(float val, const color_thresh_t *t);

void disp_sensor_link_quality(uint8_t sensor_nr, uint32_t voltage_mv, int16_t rssi_dbm);
void disp_sensor_values(uint8_t sensor_nr, sensor_type_t type, const void *payload);
void disp_sensor_offline(uint8_t sensor_nr, bool offline);

/* Von gui_actions.c benutzte Kernfunktionen (Setup Screen <-> NVS/Weatherstation-Screen) */
void apply_slot_configs(void);
void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle);
void save_sensor_slots_to_nvs(nvs_handle_t nvs_handle);
void load_basis_from_nvs(nvs_handle_t nvs_handle);
void save_basis_to_nvs(nvs_handle_t nvs_handle);

/* SEN66 - eingebauter Luftqualitaetssensor der Basisstation. Name/Icon der
 * SEN66-Karte werden wie bei den Fernsensoren im Setup Screen konfiguriert
 * (siehe apply_slot_configs() oben) - hier geht es nur um die eigentlichen
 * Messwerte (Temp/Feuchte/Feinstaub/VOC/NOx/CO2). */
void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2);
void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2);

/* SEN66-Teil des Chart-Setups (die 4 Balken-Charts PM/VOC/NOx/CO2) - ersetzt
 * das frueher gemeinsame init_charts() fuer die SEN66-Domaene. Von main.c
 * aufgerufen, unabhaengig von gui_weather_init_charts() (siehe gui_weather.h). */
void gui_sen66_init_charts(void);

/* Startet sensor_sen66_task (I2C-Messungen) - von gui_actions.c aufgerufen,
 * sobald WLAN steht. */
void gui_sen66_start_task(void);

#endif /* GUI_SENSORS_H */
