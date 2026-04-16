#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
void init_wifi(void);
esp_err_t esp_now_start(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */