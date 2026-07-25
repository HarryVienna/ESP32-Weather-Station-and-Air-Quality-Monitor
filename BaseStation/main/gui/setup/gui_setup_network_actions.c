#include "gui_setup.h"

#include "esp_lvgl_port.h"

#include "wifi/network.h"

#include "ui/ui.h"
#include "ui/actions.h"

/* WLAN-Scan/Connect-Actions des Setup Screens. Die beiden statischen
 * Callbacks existieren nur, um das Ergebnis der asynchronen WLAN-Tasks
 * (siehe wifi/network.h) auf den jeweils aufrufenden Button zurueckzuspielen
 * - je ein einziger Aufrufer, keine geteilte Business-Logik. */

static void on_wifiscan_done(char *networks)
{
  lvgl_port_lock(0);
  disp_wifi_networks(networks);
  disp_show_setup_spinner(false);
  lvgl_port_unlock();
}

void action_event_wifi_scan(lv_event_t *e)
{
  disp_show_setup_spinner(true);
  wifiscan_start(on_wifiscan_done);
}

static void on_wificonnect_done(bool connected)
{
  if (connected) {
    // Ab hier ist das die echte, dauerhafte Verbindung (nicht nur ein Test) -
    // "Starten" muss WLAN danach nicht mehr aufbauen, nur noch den Screen
    // wechseln. wifi_sync_time() blockiert bounded (siehe network.c), laeuft
    // aber hier auf dem wificonnect_task, nicht auf dem UI-Task.
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

void action_event_wifi_connect(lv_event_t *e)
{
  char network[64];
  lv_dropdown_get_selected_str(objects.dropdown_networks, network, sizeof(network));
  const char *password = lv_textarea_get_text(objects.text_area_password);

  disp_show_setup_spinner(true);
  wificonnect_start(network, password, on_wificonnect_done);
}
