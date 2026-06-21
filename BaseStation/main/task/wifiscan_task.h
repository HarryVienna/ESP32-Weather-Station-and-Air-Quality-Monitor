#ifndef WIFISCAN_TASK_H
#define WIFISCAN_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifiscan_done_cb_t)(char *networks);

void wifiscan_start(wifiscan_done_cb_t on_done);

#ifdef __cplusplus
}
#endif

#endif