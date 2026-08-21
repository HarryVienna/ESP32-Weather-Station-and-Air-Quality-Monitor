#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_system.h"

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

/* Every field in the setup screen saves to NVS immediately on change
 * (one action_event_*_value_changed() per field, wired up in EEZ Studio) -
 * instead of everything being collected and written in one go by "Start".
 * That way nothing gets lost if the device is restarted (e.g. to apply a
 * language change) before "Start" is pressed. "Start" itself is left as
 * pure navigation: apply the already-saved config to the Weatherstation
 * screen, then switch to it - see action_event_setup_start_pressed(). */

static const char *lookup_timezone(uint8_t region_id, uint8_t city_id)
{
  const char *region = regionNames[region_id];
  for (size_t i = 0; i < sizeof(cityData) / sizeof(cityData[0]); i++)
  {
    if (strcmp(cityData[i][0], region) == 0)
    {
      return cityData[i + city_id][2];
    }
  }
  return NULL;
}

static void save_region_and_timezone(uint8_t region_id, uint8_t city_id)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_uint8_to_nvs(nvs_handle, "region", region_id);
  put_uint8_to_nvs(nvs_handle, "city", city_id);
  put_string_to_nvs(nvs_handle, "tz", lookup_timezone(region_id, city_id));
  nvs_close(nvs_handle);
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
  uint8_t language = get_uint8_from_nvs(nvs_handle, "language", 0);

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

  lv_dropdown_set_selected(objects.dropdown_language, language);

  // Reflect whatever was already saved in NVS - relevant if latitude/
  // longitude are already filled in but WiFi hasn't (re-)connected yet
  // this boot (or vice versa): button_starten should stay disabled either
  // way, not just once a connect happens to succeed later.
  gui_setup_refresh_start_button();

  // A saved SSID means setup was already completed on a previous boot -
  // simulate pressing "Connect" so a plain restart with unchanged settings
  // doesn't require the user to click through Connect/Start by hand. Both
  // fields it reads (dropdown_networks/text_area_password) are already
  // populated above at this point.
  if (strcmp(ssid, "") != 0)
  {
    action_event_wifi_connect_pressed(NULL);
  }

  free(ssid);
  free(password);
  free(appid_owm);
  free(appid_vc);
  free(latitude);
  free(longitude);
  free(height);
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

  save_region_and_timezone(selectedRegion, 0);
}

void action_event_timezonecity_value_changed(lv_event_t *e)
{
  uint8_t region_id = lv_dropdown_get_selected(objects.dropdown_region);
  uint8_t city_id = lv_dropdown_get_selected(objects.dropdown_city);

  save_region_and_timezone(region_id, city_id);
}

/* Dropdown change in setup: shows the API key stored in NVS for the
 * newly selected provider in TextAreaAppId (see
 * apply_appid_for_provider()), and immediately persists the provider
 * choice itself. */
void action_event_api_value_changed(lv_event_t *e)
{
  uint8_t provider = lv_dropdown_get_selected(objects.dropdown_api);

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_uint8_to_nvs(nvs_handle, "weather_api", provider);
  char* appid_owm = get_string_from_nvs(nvs_handle, "appid_owm", "");
  char* appid_vc = get_string_from_nvs(nvs_handle, "appid_vc", "");
  nvs_close(nvs_handle);

  apply_appid_for_provider(provider, appid_owm, appid_vc);
}

/* Only the key matching the currently selected provider is written - the
 * other one stays untouched in NVS (see apply_appid_for_provider()). */
void action_event_apikey_value_changed(lv_event_t *e)
{
  uint8_t provider = lv_dropdown_get_selected(objects.dropdown_api);
  const char* appid = lv_textarea_get_text(objects.text_area_app_id);

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  switch (provider) {
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
  nvs_close(nvs_handle);
}

void action_event_lat_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "latitude", lv_textarea_get_text(objects.text_area_latitude));
  nvs_close(nvs_handle);

  gui_setup_refresh_start_button();
}

void action_event_long_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "longitude", lv_textarea_get_text(objects.text_area_longitude));
  nvs_close(nvs_handle);

  gui_setup_refresh_start_button();
}

void action_event_alt_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "height", lv_textarea_get_text(objects.text_area_hoehe));
  nvs_close(nvs_handle);
}

void action_event_language_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_uint8_to_nvs(nvs_handle, "language", lv_dropdown_get_selected(objects.dropdown_language));
  nvs_close(nvs_handle);

  // Hidden by default (see screens.c) - only shown once the language is
  // actually changed, since only then is a restart needed to see it.
  lv_obj_remove_flag(objects.restart_description, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(objects.button_restart, LV_OBJ_FLAG_HIDDEN);
}

void action_event_restart_pressed(lv_event_t *e)
{
  esp_restart();
}

void action_event_setup_start_pressed(lv_event_t *e)
{
  // Everything is already persisted (see the *_value_changed handlers
  // above and in gui_setup_network_actions.c/gui_sensors.c/gui_sen66.c),
  // and WiFi is already up (the button stays disabled until a successful
  // "Connect", see on_wificonnect_done() in gui_setup_network_actions.c) -
  // all that's left here is applying the timezone/sensor config to the
  // Weatherstation screen and switching to it.
  uint8_t region_id = lv_dropdown_get_selected(objects.dropdown_region);
  uint8_t city_id = lv_dropdown_get_selected(objects.dropdown_city);
  setenv("TZ", lookup_timezone(region_id, city_id), 1);
  tzset();

  apply_sen66_config();
  apply_sensor_slot_configs();

  loadScreen(SCREEN_ID_WEATHERSTATION_SCREEN);
}
