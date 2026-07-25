#ifndef GUI_SETUP_H
#define GUI_SETUP_H

#include <stdbool.h>

/* Setup-Screen-Kern: Kernfunktionen fuer WLAN-Auswahl/Verbindungsstatus und
 * Zeitzonen-Dropdowns. Rein intern - wird von gui_setup_network_actions.c
 * (WLAN-Scan/Connect) und gui_setup_screen_actions.c (Zeitzone, Formular
 * laden) aufgerufen (die WLAN-Tasks selbst nutzen Callback-Funktionspointer
 * statt direktem Aufruf dieser Funktionen). */

void set_cities(const char *region);
void disp_wifi_networks(char *allNetworks);
void disp_show_setup_spinner(bool show);
void disp_connect_status(bool is_connected);

#endif /* GUI_SETUP_H */
