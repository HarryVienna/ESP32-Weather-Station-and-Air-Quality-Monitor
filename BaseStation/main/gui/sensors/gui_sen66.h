#ifndef GUI_SEN66_H
#define GUI_SEN66_H

#include <stdint.h>

#include "nvs/preferences.h"

/* SEN66 - built-in air quality sensor of the base station ("base" in the
 * setup screen). Name/icon are picked from the shared catalog just like
 * the 6 remote sensors (see gui_icon_catalog.h) - this is about the actual
 * readings (temp/humidity/particulate matter/VOC/NOx/CO2) plus
 * loading/saving/applying name+icon. */
void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2);
void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2);

// SEN66 - measurement interval of the I2C task (see sensor_sen66_task.c).
// Samples/day for the 24h history charts (see gui_sen66_init_charts()) are
// derived from this.
#define SEN66_SAMPLE_INTERVAL_SEC 10
#define SEN66_HISTORY_SAMPLES_PER_DAY (24 * 60 * 60 / SEN66_SAMPLE_INTERVAL_SEC)

/* SEN66 part of the chart setup (the 4 bar charts PM/VOC/NOx/CO2) - called
 * by main.c, independent of gui_weather_init_charts() (see gui_weather.h)
 * and gui_radiation_init_chart() (see gui_sensors.h). */
void gui_sen66_init_charts(void);

/* Starts sensor_sen66_task (I2C measurements) - called by
 * gui_weatherstation_screen_actions.c as soon as the Weatherstation screen
 * loads. */
void gui_sen66_start_task(void);

/* Name+icon of the SEN66 card <-> NVS (the "base" dropdown in the setup
 * screen) - the counterpart for the 6 remote sensors is
 * load/save_sensor_slots_to_nvs() in gui_sensors.h. */
void load_basis_from_nvs(nvs_handle_t nvs_handle);
void save_basis_to_nvs(nvs_handle_t nvs_handle);

/* Applies the name+icon dropdown selected in the setup screen to the
 * SEN66 card on the Weatherstation screen. Called by
 * gui_setup_screen_actions.c right next to apply_sensor_slot_configs()
 * (see gui_sensors.h). */
void apply_sen66_config(void);

#endif /* GUI_SEN66_H */
