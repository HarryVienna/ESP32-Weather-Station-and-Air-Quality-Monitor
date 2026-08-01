#include "ui/ui.h"
#include "ui/actions.h"

#include "../status/gui_status.h"
#include "../weather/gui_weather.h"
#include "../sensors/gui_sen66.h"
#include "receiver/receiver.h"
#include "ota/ota_task.h"

/* Wired by EEZ Studio to LV_EVENT_SCREEN_LOADED of the Weatherstation
 * screen (see screens.c) - WiFi is guaranteed to be up by this point (see
 * action_event_setup_start_pressed() in gui_setup_screen_actions.c), so
 * the tasks here can start without worry. receiver_start() is deliberately
 * here instead of in main.c: the theoretical head start from an earlier
 * start gains practically nothing against the radio sensors' up to 10
 * minutes of deep sleep (see discussion), so all background tasks start
 * uniformly here. receiver_init() (creating the I2C device, sending
 * time/TZ to the slave) stays in main.c - that's I2C bus setup, not
 * starting a task. */
void action_event_weatherstation_screen_loaded(lv_event_t *e)
{
  gui_status_start_brightness_task();
  gui_status_start_clock_task();
  gui_weather_start_task();
  gui_sen66_start_task();
  receiver_start();
  ota_task_start();
}
