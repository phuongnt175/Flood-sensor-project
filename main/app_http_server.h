#ifndef __APP_HTTP_SERVER_H
#define __APP_HTTP_SERVER_H

#include <stdbool.h>

// Structure to hold device information
typedef struct {
    char macAddress[18];
    char moduleVersion[20];
    char wifiMode[10];
    char apSsid[32];
    char apIp[16];
} DeviceInfo;

DeviceInfo deviceInfo;

typedef void (*http_post_handle_t) (char *data, int len);
void start_webserver(void);
void stop_webserver(void);
void http_post_set_callback(void *cb);
#endif