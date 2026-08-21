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

/* "Start" and the connected-looking status only mean anything for the
 * SSID/password that were actually verified by a successful "Connect" -
 * touching either field afterwards must take that promise back immediately,
 * otherwise NVS can end up holding a network that was never actually
 * connected to (still shown as "ready") once the user changes their mind
 * about which network to use. WiFi itself keeps running on whatever it was
 * last told to connect to (see network.c) - only the UI's "verified" state
 * is invalidated here, not the live connection. */
static void invalidate_connection_state(void)
{
  lvgl_port_lock(0);
  disp_connect_status(false);  // also disables button_starten - see gui_setup_refresh_start_button()
  lvgl_port_unlock();
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

  invalidate_connection_state();
}

void action_event_password_value_changed(lv_event_t *e)
{
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "password", lv_textarea_get_text(objects.text_area_password));
  nvs_close(nvs_handle);

  invalidate_connection_state();
}

static void on_wificonnect_done(bool connected)
{
  if (connected) {
    // From here on this is the real, permanent connection (not just a
    // test) - "Start" no longer needs to bring up WiFi afterwards, just
    // switch the screen. The WiFi manager now retries indefinitely on its
    // own for any later disconnect (see network.c), no separate call
    // needed for that. wifi_sync_time() blocks with a bound (see
    // network.c), but runs here on the WiFi manager task, not the UI task.
    wifi_sync_time();
  }

  lvgl_port_lock(0);
  disp_connect_status(connected);  // also (re-)evaluates button_starten - see gui_setup_refresh_start_button()
  disp_show_setup_spinner(false);
  lvgl_port_unlock();
}

void action_event_wifi_connect_pressed(lv_event_t *e)
{
  char network[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, network, sizeof(network));
  const char *password = lv_textarea_get_text(objects.text_area_password);

  /* action_event_network_value_changed() only fires on an actual dropdown
   * selection change - if the wanted network is already the default (index
   * 0) after a scan repopulates the list, that event never fires and the
   * SSID never reaches NVS, even though connecting still works (it reads
   * the dropdown directly). Save it here too so whatever network was
   * actually used to connect is what survives a reboot. */
  nvs_handle_t nvs_handle;
  nvs_open("weatherstation", NVS_READWRITE, &nvs_handle);
  put_string_to_nvs(nvs_handle, "ssid", network);
  nvs_close(nvs_handle);

  disp_show_setup_spinner(true);
  wifi_connect_start(network, password, on_wificonnect_done);
}
