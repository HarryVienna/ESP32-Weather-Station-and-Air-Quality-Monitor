#include "ui/ui.h"
#include "ui/actions.h"

#include "../status/gui_status.h"
#include "../weather/gui_weather.h"
#include "../sensors/gui_sen66.h"
#include "receiver/receiver.h"
#include "ota/ota_task.h"

/* Von EEZ Studio verdrahtet auf LV_EVENT_SCREEN_LOADED des Weatherstation-
 * Screens (siehe screens.c) - WLAN steht zu diesem Zeitpunkt garantiert
 * (siehe action_event_weatherstation_start() in gui_setup_screen_actions.c),
 * deshalb koennen die Tasks hier bedenkenlos starten. receiver_start() steht
 * bewusst mit dabei statt in main.c: der vermeintliche Vorlauf durch einen
 * frueheren Start bringt gegen die bis zu 10 Minuten Tiefschlaf der
 * Funksensoren praktisch nichts (siehe Diskussion), also starten alle
 * Hintergrund-Tasks einheitlich hier. receiver_init() (I2C-Geraet anlegen,
 * Zeit/TZ an den Slave senden) bleibt in main.c - das ist Setup des
 * I2C-Bus, kein Task-Start. */
void action_event_weatherstation_screen_loaded(lv_event_t *e)
{
  gui_status_start_brightness_task();
  gui_status_start_clock_task();
  gui_weather_start_task();
  gui_sen66_start_task();
  receiver_start();
  ota_task_start();
}
