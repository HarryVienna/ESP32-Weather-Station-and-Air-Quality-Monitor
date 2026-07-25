#ifndef GUI_SEN66_H
#define GUI_SEN66_H

#include <stdint.h>

#include "nvs/preferences.h"

/* SEN66 - eingebauter Luftqualitaetssensor der Basisstation ("Basis" im
 * Setup Screen). Name/Icon werden wie bei den 6 Fernsensoren aus dem
 * gemeinsamen Katalog gewaehlt (siehe gui_icon_catalog.h) - hier geht es um
 * die eigentlichen Messwerte (Temp/Feuchte/Feinstaub/VOC/NOx/CO2) sowie
 * Laden/Speichern/Anwenden von Name+Icon. */
void disp_sen6x(float ambientTemperature, float ambientHumidity, float massConcentrationPm1p0, float massConcentrationPm2p5, float massConcentrationPm4p0, float massConcentrationPm10p0, float vocIndex, float noxIndex, uint16_t co2);
void update_sen66_charts(float pm1, float pm2p5, float pm4, float pm10, float voc, float nox, uint16_t co2);

// SEN66 - Messintervall der I2C-Task (siehe sensor_sen66_task.c). Fuer die
// 24h-Verlaufscharts (siehe gui_sen66_init_charts()) daraus Samples/Tag
// abgeleitet.
#define SEN66_SAMPLE_INTERVAL_SEC 10
#define SEN66_HISTORY_SAMPLES_PER_DAY (24 * 60 * 60 / SEN66_SAMPLE_INTERVAL_SEC)

/* SEN66-Teil des Chart-Setups (die 4 Balken-Charts PM/VOC/NOx/CO2) - von
 * main.c aufgerufen, unabhaengig von gui_weather_init_charts() (siehe
 * gui_weather.h) und gui_radiation_init_chart() (siehe gui_sensors.h). */
void gui_sen66_init_charts(void);

/* Startet sensor_sen66_task (I2C-Messungen) - von
 * gui_weatherstation_screen_actions.c aufgerufen, sobald der Weatherstation-
 * Screen laedt. */
void gui_sen66_start_task(void);

/* Name+Icon der SEN66-Karte <-> NVS (Dropdown "Basis" im Setup Screen) -
 * Gegenstueck fuer die 6 Fernsensoren sind load/save_sensor_slots_to_nvs()
 * in gui_sensors.h. */
void load_basis_from_nvs(nvs_handle_t nvs_handle);
void save_basis_to_nvs(nvs_handle_t nvs_handle);

/* Uebertraegt das im Setup Screen gewaehlte Name+Icon-Dropdown auf die
 * SEN66-Karte im Weatherstation-Screen. Wird von gui_setup_screen_actions.c
 * direkt neben apply_sensor_slot_configs() (siehe gui_sensors.h) aufgerufen. */
void apply_sen66_config(void);

#endif /* GUI_SEN66_H */
