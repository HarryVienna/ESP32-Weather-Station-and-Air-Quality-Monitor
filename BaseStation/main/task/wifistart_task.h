#ifndef WIFISTART_TASK_H
#define WIFISTART_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifistart_done_cb_t)(void);

void wifistart_start(wifistart_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif
