#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
esp_err_t init_wifi(void);
esp_err_t esp_now_start(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
