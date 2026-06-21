#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
void wifi_init(void);
bool wifi_connect(const char* ssid, const char* password, bool retry_forever);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */