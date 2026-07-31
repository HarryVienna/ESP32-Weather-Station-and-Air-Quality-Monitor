#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
esp_err_t wifi_init(void);

/* Connects with ssid/password, waits bounded (WIFI_CONNECT_MAX_RETRIES
 * attempts, see network.c) and returns whether it worked. Same behavior
 * for the test connect (setup screen button) and the initial connection
 * on startup - there's no difference between the two, hence no parameter
 * for it. For permanent reconnect behavior afterwards, see
 * wifi_stay_connected_forever(). */
bool wifi_connect(const char* ssid, const char* password);

/* From now on, the WiFi driver retries connecting indefinitely on every
 * WIFI_EVENT_STA_DISCONNECTED (instead of giving up after
 * WIFI_CONNECT_MAX_RETRIES) - e.g. if only the router restarts, the
 * weather station should never permanently give up during operation.
 * Called by the setup screen "Connect" button on success (see
 * gui_setup_network_actions.c) - from that point on this IS the real,
 * permanent connection; the "Start" button no longer needs to bring up
 * WiFi separately afterwards. */
void wifi_stay_connected_forever(void);

/* Blocking, bounded (10s timeout): synchronizes the system time via NTP.
 * Called by the setup screen "Connect" button after a successful
 * wifi_connect() (see gui_setup_network_actions.c) - already runs there on
 * its own task (wificonnect_task), so it doesn't block the UI. Calling it
 * multiple times is safe (SNTP is only initialized the first time). */
void wifi_sync_time(void);

/* The following start their respective WiFi operation as its own FreeRTOS
 * task, so the (blocking) esp_wifi_* call doesn't block the caller (e.g.
 * an LVGL event handler). on_done runs on the respective task, not the
 * caller - lock when accessing the UI inside it. */

typedef void (*wifiscan_done_cb_t)(char *networks);

/* One-shot WiFi scan (setup screen dropdown). on_done gets the found
 * SSIDs as a newline-separated list (can be empty). */
void wifiscan_start(wifiscan_done_cb_t on_done);

typedef void (*wificonnect_done_cb_t)(bool connected);

/* Connects ssid/password (setup screen "Connect" button), bounded (see
 * wifi_connect()). This is now the only place that actually brings up
 * WiFi - the caller should call wifi_stay_connected_forever() and
 * wifi_sync_time() on success (see gui_setup_network_actions.c:
 * on_wificonnect_done()). */
void wificonnect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */