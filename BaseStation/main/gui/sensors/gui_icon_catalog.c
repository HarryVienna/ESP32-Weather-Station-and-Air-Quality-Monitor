#include "gui_icon_catalog.h"

#include "ui/ui.h"

/* ============================================================================
 * Name+Icon-Katalog (SEN66 "Basis" + 6 Fernsensor-Slots)
 *
 * Jede Zeile ist eine Wahlmoeglichkeit im Setup-Screen-Dropdown. Label =
 * Anzeigename auf der Sensor-Karte, icon = zugehoeriges Bild. Neuen Eintrag
 * hinzufuegen = neue Zeile, fertig - wird automatisch in allen 7 Dropdowns
 * (Basis + 6 Sensoren) angeboten.
 * ============================================================================ */

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
#define SENSOR_ICON_ARR_COUNT (sizeof(sensor_icon_options) / sizeof(sensor_icon_options[0]))

size_t sensor_icon_count(void)
{
  return SENSOR_ICON_ARR_COUNT;
}

const sensor_icon_option_t *sensor_icon_option(size_t index)
{
  return (index < SENSOR_ICON_ARR_COUNT) ? &sensor_icon_options[index] : &sensor_icon_options[0];
}

void populate_sensor_icon_dropdown(lv_obj_t *dropdown)
{
  lv_dropdown_clear_options(dropdown);
  for (size_t i = 0; i < SENSOR_ICON_ARR_COUNT; i++) {
    lv_dropdown_add_option(dropdown, sensor_icon_options[i].label, LV_DROPDOWN_POS_LAST);
  }
}
