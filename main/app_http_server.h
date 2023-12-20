#ifndef __APP_HTTP_SERVER_H
#define __APP_HTTP_SERVER_H

typedef struct {
    char macAddress[18];
    char moduleVersion[20];
    char wifiMode[10];
    char apSsid[32];
    char apIp[16];
    char staDHCP[16];
    char staIP[16];
    char staSubMask[20];
    char staGateway[20];
    char staDNS[20];
} DeviceInfo;

DeviceInfo deviceInfo;

typedef void (*http_post_handle_t) (char *data, int len);
void start_webserver(void);
void stop_webserver(void);
void http_post_set_callback(void *cb);
#endif