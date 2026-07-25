#ifndef GUI_ICON_CATALOG_H
#define GUI_ICON_CATALOG_H

#include <stddef.h>

#include "lvgl.h"

/* Name+Icon-Katalog fuer die Setup-Screen-Dropdowns: SEN66 ("Basis", siehe
 * gui_sen66.c) und jeder der 6 Fernsensor-Slots (siehe gui_sensors.c)
 * waehlen hier ihren Anzeigenamen+ihr Icon gemeinsam aus einem Dropdown aus. */
typedef struct {
  const char *label;
  const lv_img_dsc_t *icon;
} sensor_icon_option_t;

size_t sensor_icon_count(void);

/* Liefert den Katalog-Eintrag zu index, oder Eintrag 0 falls index ausserhalb
 * des Katalogs liegt (z.B. verwaister NVS-Wert nach Katalog-Aenderung). */
const sensor_icon_option_t *sensor_icon_option(size_t index);

/* Befuellt ein Dropdown-Widget mit allen Eintraegen aus obigem Katalog - von
 * load_basis_from_nvs() (gui_sen66.c) und load_sensor_slots_from_nvs()
 * (gui_sensors.c) beim Laden aus NVS genutzt. */
void populate_sensor_icon_dropdown(lv_obj_t *dropdown);

#endif /* GUI_ICON_CATALOG_H */
