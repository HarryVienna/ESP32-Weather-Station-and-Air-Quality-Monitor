#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
void init_wifi(void);
void esp_now_start();

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */