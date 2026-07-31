#ifndef GUI_ICON_CATALOG_H
#define GUI_ICON_CATALOG_H

#include <stddef.h>

#include "lvgl.h"

/* Name+icon catalog for the setup screen dropdowns: SEN66 ("base", see
 * gui_sen66.c) and each of the 6 remote sensor slots (see gui_sensors.c)
 * pick their display name+icon together from a single dropdown. */
typedef struct {
  const char *label;
  const lv_img_dsc_t *icon;
} sensor_icon_option_t;

size_t sensor_icon_count(void);

/* Returns the catalog entry for index, or entry 0 if index is outside the
 * catalog (e.g. an orphaned NVS value after a catalog change). */
const sensor_icon_option_t *sensor_icon_option(size_t index);

/* Fills a dropdown widget with all entries from the catalog above - used
 * by load_basis_from_nvs() (gui_sen66.c) and load_sensor_slots_from_nvs()
 * (gui_sensors.c) when loading from NVS. */
void populate_sensor_icon_dropdown(lv_obj_t *dropdown);

#endif /* GUI_ICON_CATALOG_H */
