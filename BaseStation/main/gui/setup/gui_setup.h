#ifndef GUI_SETUP_H
#define GUI_SETUP_H

#include <stdbool.h>

/* Setup screen core: core functions for WiFi selection/connection status
 * and timezone dropdowns. Purely internal - called by
 * gui_setup_network_actions.c (WiFi scan/connect) and
 * gui_setup_screen_actions.c (timezone, loading the form) (the WiFi tasks
 * themselves use callback function pointers instead of calling these
 * functions directly). */

void set_cities(const char *region);
void disp_wifi_networks(char *allNetworks);
void disp_show_setup_spinner(bool show);
void disp_connect_status(bool is_connected);

/* "Start" needs BOTH a verified WiFi connection (tracked internally, set by
 * disp_connect_status()) AND usable latitude/longitude - otherwise the
 * Weatherstation screen loads fine but weather_task() immediately refuses
 * to start (see validate_coordinates() in gui/weather/weather_task.h),
 * silently leaving the weather panel empty forever. Re-evaluates and
 * enables/disables objects.button_starten accordingly - call this whenever
 * either condition could have changed: after disp_connect_status(), after
 * the latitude/longitude fields change, and once when the setup screen
 * loads (to reflect whatever was already saved in NVS). */
void gui_setup_refresh_start_button(void);

#endif /* GUI_SETUP_H */
