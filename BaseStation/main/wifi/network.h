#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
void wifi_init(void);

/* Verbindet mit ssid/password, wartet bounded (WIFI_CONNECT_MAX_RETRIES
 * Versuche, siehe network.c) und gibt zurueck, ob es geklappt hat. Gleiches
 * Verhalten fuer Test-Connect (Setup-Screen-Button) und den ersten
 * Verbindungsaufbau beim Starten - kein Unterschied zwischen den beiden,
 * daher kein Parameter dafuer. Fuer dauerhaftes Reconnect-Verhalten danach
 * siehe wifi_stay_connected_forever(). */
bool wifi_connect(const char* ssid, const char* password);

/* Ab jetzt versucht der WLAN-Treiber bei jedem WIFI_EVENT_STA_DISCONNECTED
 * unbegrenzt oft neu zu verbinden (statt nach WIFI_CONNECT_MAX_RETRIES
 * aufzugeben) - z.B. wenn nur der Router neu startet, soll die
 * Wetterstation im laufenden Betrieb nie endgueltig aufgeben. Vom
 * Setup-Screen-"Verbinden"-Button bei Erfolg aufgerufen (siehe
 * gui_actions.c) - ab da IST das die echte, dauerhafte Verbindung; der
 * "Starten"-Button muss WLAN danach nicht mehr extra aufbauen. */
void wifi_stay_connected_forever(void);

/* Blockierend, bounded (10s Timeout): synchronisiert die Systemzeit per NTP.
 * Vom Setup-Screen-"Verbinden"-Button nach erfolgreichem wifi_connect()
 * aufgerufen (siehe gui_actions.c) - laeuft dort bereits auf einem eigenen
 * Task (wificonnect_task), blockiert also nicht die UI. Mehrfacher Aufruf
 * ist sicher (SNTP wird nur beim ersten Mal initialisiert). */
void wifi_sync_time(void);

/* Die folgenden starten die jeweilige WLAN-Operation als eigenen
 * FreeRTOS-Task, damit der (blockierende) esp_wifi_*-Aufruf nicht den
 * Aufrufer (z.B. einen LVGL-Event-Handler) blockiert. on_done laeuft auf
 * dem jeweiligen Task, nicht dem Aufrufer - bei UI-Zugriff drin locken. */

typedef void (*wifiscan_done_cb_t)(char *networks);

/* Einmaliger WLAN-Scan (Setup-Screen-Dropdown). on_done bekommt die
 * gefundenen SSIDs als newline-getrennte Liste (kann leer sein). */
void wifiscan_start(wifiscan_done_cb_t on_done);

typedef void (*wificonnect_done_cb_t)(bool connected);

/* Verbindet ssid/password (Setup-Screen-"Verbinden"-Button), bounded (siehe
 * wifi_connect()). Das ist mittlerweile die einzige Stelle, die WLAN wirklich
 * aufbaut - der Aufrufer sollte bei Erfolg wifi_stay_connected_forever() und
 * wifi_sync_time() aufrufen (siehe gui_actions.c: on_wificonnect_done()). */
void wificonnect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */