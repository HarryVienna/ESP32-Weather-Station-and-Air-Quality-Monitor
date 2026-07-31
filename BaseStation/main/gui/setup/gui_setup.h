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

#endif /* GUI_SETUP_H */
