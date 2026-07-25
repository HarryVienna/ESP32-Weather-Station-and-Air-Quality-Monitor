#ifndef GUI_SENSORS_H
#define GUI_SENSORS_H

#include <stdint.h>
#include <stdbool.h>

#include "nvs/preferences.h"
#include "../../../common/packet_format.h"

/* Sensoren - 6 Fernsensoren (LoRa/ESP-NOW). Die gemeinsamen Wert->Farbe-
 * Funktionen liegen in gui_color_scale.h, der Name+Icon-Katalog in
 * gui_icon_catalog.h und das 24h-Verlaufschart-Bucketing in
 * gui_history_chart.h (alle drei auch von SEN66 gebraucht, siehe
 * gui_sen66.h) - hier geht es nur um die 6 fest verbauten Sensor-Karten im
 * Weatherstation-Screen.
 *
 * -----------------------------------------------------------------------
 * Wie fuege ich einen Sensor hinzu / aendere einen bestehenden?
 * -----------------------------------------------------------------------
 * "Ein Sensor" besteht aus zwei unabhaengigen Dingen:
 *
 * a) WAS man im Setup Screen auswaehlen kann (Name+Icon-Katalog): siehe
 *    sensor_icon_options[] in gui_icon_catalog.c. Ein Dropdown legt Name UND
 *    Icon gemeinsam fest - es gibt keine separaten Namensfelder. Neues Icon
 *    zur Auswahl hinzufuegen = eine neue Zeile dort, sonst nichts.
 *
 * b) WELCHER Sensor-Typ (bme280/sht45/geiger) an WELCHER Stelle im
 *    Weatherstation-Screen fest verbaut ist: sensor_slots[] in
 *    gui_sensors.c. Jeder der 6 Sensor_X-Widgets (in EEZ Studio direkt so
 *    benannt, X=0..5) hat dauerhaft einen fest verbauten Widget-Typ. Diese
 *    Tabelle sagt disp_sensor_values()/disp_sensor_link_quality(), welche
 *    von EEZ generierten Feldnamen (objects.sensor_N__temp usw.) zu welchem
 *    Slot gehoeren. Aendert sich in EEZ Studio, welcher Typ in Slot X
 *    verbaut ist, wird NUR diese eine Tabellenzeile angepasst - der Rest
 *    der Datei bleibt unberuehrt.
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

void disp_sensor_link_quality(uint8_t sensor_nr, uint32_t voltage_mv, int16_t rssi_dbm);
void disp_sensor_values(uint8_t sensor_nr, sensor_type_t type, const void *payload);
void disp_sensor_offline(uint8_t sensor_nr, bool offline);

/* Von gui_setup_screen_actions.c benutzte Kernfunktionen (Setup Screen <->
 * NVS/Weatherstation-Screen). Gegenstueck fuer SEN66 ("Basis") sind
 * load/save_basis_to_nvs() und apply_sen66_config() in gui_sen66.h. */
void apply_sensor_slot_configs(void);
void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle);
void save_sensor_slots_to_nvs(nvs_handle_t nvs_handle);

/* Geigerzaehler - Sende-Intervall (auf der Sensor-Seite fest verdrahtet, von
 * der Basisstation aus nicht steuerbar - anders als SEN66_SAMPLE_INTERVAL_SEC
 * in gui_sen66.h). Fuer die 24h-Verlaufschart (siehe gui_radiation_init_chart())
 * daraus Samples/Tag abgeleitet. */
#define RADIATION_SAMPLE_INTERVAL_SEC 60
#define RADIATION_HISTORY_SAMPLES_PER_DAY (24 * 60 * 60 / RADIATION_SAMPLE_INTERVAL_SEC)

/* Geigerzaehler (Slot 5) - 24h-Verlaufschart. Wie gui_sen66_init_charts()
 * (siehe gui_sen66.h) einmalig beim Start aufrufen, bevor Pakete ueber
 * disp_sensor_values() reinkommen. */
void gui_radiation_init_chart(void);

#endif /* GUI_SENSORS_H */
