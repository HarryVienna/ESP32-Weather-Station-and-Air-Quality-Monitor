#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_time_synced_cb_t)(void);

/* time_synced_cb, if not NULL, fires every time SNTP updates the system
 * clock - once for the initial sync triggered by wifi_sync_time(), and
 * again on every periodic resync afterwards (SNTP_OPMODE_POLL keeps
 * polling in the background for the device's entire uptime, see
 * network.c: sync_callback()). Use this instead of only reacting to the
 * one-shot wifi_sync_time() call if something needs to stay in sync
 * continuously, not just at startup (e.g. receiver_sync_time() in
 * receiver/receiver.h, passed in from main.c).
 *
 * Also creates the WiFi manager's queue + task (see network.c) - the
 * single owner of all esp_wifi_* calls; wifi_connect_start()/
 * wifiscan_start() below just post requests to it. */
esp_err_t wifi_init(wifi_time_synced_cb_t time_synced_cb);

/* Blocking, bounded (10s timeout): synchronizes the system time via NTP.
 * Called by the setup screen "Connect" button after a successful
 * wifi_connect_start() (see gui_setup_network_actions.c) - runs on the
 * WiFi manager task, not the UI task, so it doesn't block the UI. Calling
 * it multiple times is safe (SNTP is only initialized the first time). */
void wifi_sync_time(void);

/* The following post their respective request to the WiFi manager task
 * (see network.c) and return immediately - on_done runs on that task, not
 * the caller, so lock when accessing the UI inside it. */

typedef void (*wifiscan_done_cb_t)(char *networks);

/* One-shot WiFi scan (setup screen dropdown). on_done gets the found
 * SSIDs as a newline-separated list (can be empty). If a connection is up
 * when this is called, it's resumed automatically once the scan is done -
 * scanning doesn't require staying disconnected afterwards. */
void wifiscan_start(wifiscan_done_cb_t on_done);

typedef void (*wificonnect_done_cb_t)(bool connected);

/* Connects ssid/password (setup screen "Connect" button, and the boot-time
 * auto-connect for a previously saved SSID - both go through this, there's
 * no behavioral difference between them). Bounded retries
 * (WIFI_CONNECT_MAX_RETRIES, see network.c) while reaching the first
 * successful connection; from then on the WiFi manager retries indefinitely
 * on every later disconnect (e.g. the router rebooting must never be
 * treated as "give up") without calling on_done again - that only fires
 * once, for this initial attempt. */
void wifi_connect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
