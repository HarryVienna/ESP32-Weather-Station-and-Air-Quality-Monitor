#include "gui_setup.h"

#include <string.h>

#include "config/config.h"
#include "ui/ui.h"
#include "timezone_data.h"
#include "gui/weather/weather_task.h"

/* Set by disp_connect_status(), read by gui_setup_refresh_start_button() -
 * see the rationale on that function's declaration in gui_setup.h. */
static bool s_wifi_connected = false;

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

  s_wifi_connected = is_connected;
  gui_setup_refresh_start_button();
}

void gui_setup_refresh_start_button(void)
{
  const char *latitude = lv_textarea_get_text(objects.text_area_latitude);
  const char *longitude = lv_textarea_get_text(objects.text_area_longitude);

  if (s_wifi_connected && validate_coordinates(latitude, longitude))
  {
    lv_obj_remove_state(objects.button_starten, LV_STATE_DISABLED);
  }
  else
  {
    lv_obj_add_state(objects.button_starten, LV_STATE_DISABLED);
  }
}
