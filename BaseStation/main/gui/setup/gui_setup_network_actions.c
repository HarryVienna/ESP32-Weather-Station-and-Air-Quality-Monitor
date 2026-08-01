#include "gui_setup.h"

#include "esp_lvgl_port.h"

#include "nvs/preferences.h"
#include "wifi/network.h"

#include "ui/ui.h"
#include "ui/actions.h"

/* WiFi scan/connect actions for the setup screen. The two static
 * callbacks exist only to relay the result of the asynchronous WiFi tasks
 * (see wifi/network.h) back to the button that called them - each has a
 * single caller, no shared business logic. */

static void on_wifiscan_done(char *networks)
{
  lvgl_port_lock(0);
  disp_wifi_networks(networks);
  disp_show_setup_spinner(false);
  lvgl_port_unlock();
}

void action_event_wifi_scan_pressed(lv_event_t *e)
{
  disp_show_setup_spinner(true);
  wifiscan_start(on_wifiscan_done);
}

/* Instant-save (see gui_setup_screen_actions.c for the general rationale):
 * SSID/password persist as soon as they change, independent of "Connect"/
 * "Start" - just storage, no attempt to actually use them yet. */
void action_event_network_value_changed(lv_event_t *e)
{
  char ssid[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, ssid, sizeof(ssid));

  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "ssid", ssid);
  nvs_close(nvs_handle);
}

void action_event_password_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "password", lv_textarea_get_text(objects.text_area_password));
  nvs_close(nvs_handle);
}

static void on_wificonnect_done(bool connected)
{
  if (connected) {
    // From here on this is the real, permanent connection (not just a
    // test) - "Start" no longer needs to bring up WiFi afterwards, just
    // switch the screen. wifi_sync_time() blocks with a bound (see
    // network.c), but runs here on the wificonnect_task, not the UI task.
    wifi_stay_connected_forever();
    wifi_sync_time();
  }

  lvgl_port_lock(0);
  disp_connect_status(connected);
  disp_show_setup_spinner(false);
  if (connected) {
    lv_obj_remove_state(objects.button_starten, LV_STATE_DISABLED);
  }
  lvgl_port_unlock();
}

void action_event_wifi_connect_pressed(lv_event_t *e)
{
  char network[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, network, sizeof(network));
  const char *password = lv_textarea_get_text(objects.text_area_password);

  disp_show_setup_spinner(true);
  wificonnect_start(network, password, on_wificonnect_done);
}
