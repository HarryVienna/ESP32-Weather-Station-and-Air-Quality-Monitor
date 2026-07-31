#ifndef GUI_SENSORS_H
#define GUI_SENSORS_H

#include <stdint.h>
#include <stdbool.h>

#include "nvs/preferences.h"
#include "../../../common/packet_format.h"

/* Sensors - 6 remote sensors (LoRa/ESP-NOW). The shared value->color
 * functions live in gui_color_scale.h, the name+icon catalog in
 * gui_icon_catalog.h, and the 24h history chart bucketing in
 * gui_history_chart.h (all three also used by SEN66, see gui_sen66.h) -
 * this file is only about the 6 hardwired sensor cards in the
 * Weatherstation screen.
 *
 * -----------------------------------------------------------------------
 * How do I add a sensor / change an existing one?
 * -----------------------------------------------------------------------
 * "A sensor" consists of two independent things:
 *
 * a) WHAT you can select in the setup screen (name+icon catalog): see
 *    sensor_icon_options[] in gui_icon_catalog.c. One dropdown sets name
 *    AND icon together - there are no separate name fields. Adding a new
 *    icon to choose from = one new row there, nothing else.
 *
 * b) WHICH sensor type (bme280/sht45/geiger) is hardwired at WHICH slot in
 *    the Weatherstation screen: sensor_slots[] in gui_sensors.c. Each of
 *    the 6 Sensor_X widgets (named that way directly in EEZ Studio,
 *    X=0..5) permanently has a fixed widget type. This table tells
 *    disp_sensor_values()/disp_sensor_link_quality() which of the
 *    EEZ-generated field names (objects.sensor_N__temp etc.) belong to
 *    which slot. If the type installed in slot X changes in EEZ Studio,
 *    ONLY that one table row needs to be adjusted - the rest of the file
 *    stays untouched.
 *
 * The slot index (0-5) is the same everywhere: the UI order "Sensor 1..6"
 * in the setup screen, packet_header_t.sensor_nr in the radio packet (see
 * common/packet_format.h), and the index in sensor_slots[]/
 * sensor_dropdown_widgets[]. */

/* Number of hardwired sensor cards in the Weatherstation screen (see
 * sensor_slots[] in gui_sensors.c). sensor_nr from packet_header_t must be
 * < this value to be mapped to a card - also used by the receiver watchdog
 * to know how many slots to monitor. */
#define SENSOR_SLOT_COUNT 6

void disp_sensor_link_quality(uint8_t sensor_nr, uint32_t voltage_mv, int16_t rssi_dbm);
void disp_sensor_values(uint8_t sensor_nr, sensor_type_t type, const void *payload);
void disp_sensor_offline(uint8_t sensor_nr, bool offline);

/* Core functions used by gui_setup_screen_actions.c (setup screen <->
 * NVS/Weatherstation screen). The counterpart for SEN66 ("base") is
 * load/save_basis_to_nvs() and apply_sen66_config() in gui_sen66.h. */
void apply_sensor_slot_configs(void);
void load_sensor_slots_from_nvs(nvs_handle_t nvs_handle);
void save_sensor_slots_to_nvs(nvs_handle_t nvs_handle);

/* Geiger counter - send interval (hardwired on the sensor side, not
 * controllable from the base station - unlike SEN66_SAMPLE_INTERVAL_SEC in
 * gui_sen66.h). Samples/day for the 24h history chart (see
 * gui_radiation_init_chart()) are derived from this. */
#define RADIATION_SAMPLE_INTERVAL_SEC 60
#define RADIATION_HISTORY_SAMPLES_PER_DAY (24 * 60 * 60 / RADIATION_SAMPLE_INTERVAL_SEC)

/* Geiger counter (slot 5) - 24h history chart. Call once at startup like
 * gui_sen66_init_charts() (see gui_sen66.h), before packets start coming in
 * via disp_sensor_values(). */
void gui_radiation_init_chart(void);

#endif /* GUI_SENSORS_H */
