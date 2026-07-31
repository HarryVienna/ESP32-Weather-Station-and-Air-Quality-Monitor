#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "nvs/preferences.h"

#include "ui/ui.h"
#include "ui/actions.h"

#include "gui_setup.h"
#include "timezone_data.h"
#include "../sensors/gui_sensors.h"
#include "../sensors/gui_sen66.h"
#include "weather/weather_provider.h"

static const char* TAG = "gui_setup_screen_actions";

/* One text field (TextAreaAppId) for two API keys (OpenWeatherMap/
 * VisualCrossing) - depending on the DropdownApi selection, the matching
 * key stored in NVS is shown. Open-Meteo doesn't need a key, so the field
 * is cleared and disabled for it. Used by
 * action_event_setup_screen_loaded() and action_event_api_value_changed()
 * (dropdown change). */
static void apply_appid_for_provider(uint8_t provider, const char *appid_owm, const char *appid_vc)
{
  switch (provider) {
    case WEATHER_PROVIDER_OPENWEATHERMAP:
      lv_textarea_set_text(objects.text_area_app_id, appid_owm);
      lv_obj_clear_state(objects.text_area_app_id, LV_STATE_DISABLED);
      break;
    case WEATHER_PROVIDER_VISUALCROSSING:
      lv_textarea_set_text(objects.text_area_app_id, appid_vc);
      lv_obj_clear_state(objects.text_area_app_id, LV_STATE_DISABLED);
      break;
    case WEATHER_PROVIDER_OPEN_METEO:
    default:
      lv_textarea_set_text(objects.text_area_app_id, "");
      lv_obj_add_state(objects.text_area_app_id, LV_STATE_DISABLED);
      break;
  }
}

void action_event_setup_screen_loaded(lv_event_t *e)
{
  // EEZ Studio doesn't mark this panel hidden by default, unlike the keyboards
  disp_show_setup_spinner(false);

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);

  char* ssid = get_string_from_nvs(nvs_handle, "ssid", "");
  char* password = get_string_from_nvs(nvs_handle, "password", "");
  uint8_t weather_provider = get_uint8_from_nvs(nvs_handle, "weather_api", 0);
  char* appid_owm = get_string_from_nvs(nvs_handle, "appid_owm", "");
  char* appid_vc = get_string_from_nvs(nvs_handle, "appid_vc", "");
  char* latitude = get_string_from_nvs(nvs_handle, "latitude", "");
  char* longitude =get_string_from_nvs(nvs_handle, "longitude", "");
  char* height = get_string_from_nvs(nvs_handle, "height", "");
  uint8_t region_id = get_uint8_from_nvs(nvs_handle, "region", 0);
  uint8_t city_id = get_uint8_from_nvs(nvs_handle, "city", 0);

  load_basis_from_nvs(nvs_handle);
  load_sensor_slots_from_nvs(nvs_handle);

  nvs_close(nvs_handle);

  if (strcmp(ssid, "") != 0)
  {
    lv_dropdown_clear_options(objects.dropdown_networks);
    lv_dropdown_add_option(objects.dropdown_networks, ssid, LV_DROPDOWN_POS_LAST);
  }
  lv_textarea_set_text(objects.text_area_password, password);

  lv_dropdown_set_selected(objects.dropdown_api, weather_provider);
  apply_appid_for_provider(weather_provider, appid_owm, appid_vc);

  lv_textarea_set_text(objects.text_area_latitude, latitude);
  lv_textarea_set_text(objects.text_area_longitude, longitude);
  lv_textarea_set_text(objects.text_area_hoehe, height);

  // fill the region names
  for (size_t i = 0; i < sizeof(regionNames) / sizeof(regionNames[0]); i++)
  {
    lv_dropdown_add_option(objects.dropdown_region, regionNames[i], LV_DROPDOWN_POS_LAST);
  }
  lv_dropdown_set_selected(objects.dropdown_region, region_id); // set the selected region id
  // get the region name
  char region[64];
  lv_dropdown_get_selected_str(objects.dropdown_region, region, sizeof(region));
  // fill the city list
  set_cities(region);
  // set the selected city id
  lv_dropdown_set_selected(objects.dropdown_city, city_id);
}

void action_event_timezone_value_changed(lv_event_t *e)
{
  int selectedRegion = lv_dropdown_get_selected(objects.dropdown_region);
  ESP_LOGI(TAG,"selectedRegion: %d", selectedRegion);

  char region[64];
  lv_dropdown_get_selected_str(objects.dropdown_region, region, sizeof(region));
  ESP_LOGI(TAG,"region: %s", region);

  set_cities(region);

  lv_dropdown_set_selected(objects.dropdown_city, 0);
}

/* Dropdown change in setup: shows the API key stored in NVS for the
 * newly selected provider in TextAreaAppId (see
 * apply_appid_for_provider()). Unsaved input for the previously selected
 * provider is lost if "Start" wasn't pressed before switching. */
void action_event_api_value_changed(lv_event_t *e)
{
  uint8_t provider = lv_dropdown_get_selected(objects.dropdown_api);

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
  char* appid_owm = get_string_from_nvs(nvs_handle, "appid_owm", "");
  char* appid_vc = get_string_from_nvs(nvs_handle, "appid_vc", "");
  nvs_close(nvs_handle);

  apply_appid_for_provider(provider, appid_owm, appid_vc);
}

void action_event_weatherstation_start(lv_event_t *e)
{
  // Store preferences
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);

  char ssid[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, ssid, sizeof(ssid));
  put_string_to_nvs(nvs_handle, "ssid", ssid);

  const char* password = lv_textarea_get_text(objects.text_area_password);
  put_string_to_nvs(nvs_handle, "password", password);

  uint8_t weather_provider = lv_dropdown_get_selected(objects.dropdown_api);
  put_uint8_to_nvs(nvs_handle, "weather_api", weather_provider);

  // Only the key matching the selected provider is written - the other
  // one stays untouched in NVS (see apply_appid_for_provider()).
  const char* appid = lv_textarea_get_text(objects.text_area_app_id);
  switch (weather_provider) {
    case WEATHER_PROVIDER_OPENWEATHERMAP:
      put_string_to_nvs(nvs_handle, "appid_owm", appid);
      break;
    case WEATHER_PROVIDER_VISUALCROSSING:
      put_string_to_nvs(nvs_handle, "appid_vc", appid);
      break;
    case WEATHER_PROVIDER_OPEN_METEO:
    default:
      break; // no key needed
  }

  const char* latitude = lv_textarea_get_text(objects.text_area_latitude);
  put_string_to_nvs(nvs_handle, "latitude", latitude);

  const char* longitude = lv_textarea_get_text(objects.text_area_longitude);
  put_string_to_nvs(nvs_handle, "longitude", longitude);

  const char* height = lv_textarea_get_text(objects.text_area_hoehe);
  put_string_to_nvs(nvs_handle, "height", height);

  uint8_t region_id = lv_dropdown_get_selected(objects.dropdown_region);
  put_uint8_to_nvs(nvs_handle, "region", region_id);

  uint8_t city_id = lv_dropdown_get_selected(objects.dropdown_city);
  put_uint8_to_nvs(nvs_handle, "city", city_id);

  const char* tz = NULL;
  const char *region = regionNames[region_id];
  for (size_t i = 0; i < sizeof(cityData) / sizeof(cityData[0]); i++)
  {
    if (strcmp(cityData[i][0], region) == 0)
    {
      tz = cityData[i + city_id][2];
      break;
    }
  }
  put_string_to_nvs(nvs_handle, "tz", tz);

  save_basis_to_nvs(nvs_handle);
  save_sensor_slots_to_nvs(nvs_handle);

  nvs_close(nvs_handle);

  // WiFi is already up (the button stays disabled until a successful
  // "Connect", see on_wificonnect_done() in gui_setup_network_actions.c) -
  // all that's left here is applying the timezone and switching to the
  // Weatherstation screen.
  setenv("TZ", tz, 1);
  tzset();

  apply_sen66_config();
  apply_sensor_slot_configs();

  loadScreen(SCREEN_ID_WEATHERSTATION_SCREEN);
}
