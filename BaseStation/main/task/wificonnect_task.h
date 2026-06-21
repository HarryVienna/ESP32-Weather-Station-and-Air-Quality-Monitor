#ifndef WIFICONNECT_TASK_H
#define WIFICONNECT_TASK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wificonnect_done_cb_t)(bool connected);

void wificonnect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif