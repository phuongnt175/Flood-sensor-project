#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

typedef enum
{
    PROVISION_ACCESSPOINT = 0,
    PROVISION_SMARTCONFIG = 1,
    PROVISION_AP = 2,
} provision_type_t;

// Structure to hold AP configuration
typedef struct {
    char ssid[32];
    char password[64];
    char channel[10];
} ap_config_t;

void app_config(void);
void ap_start(void);
#endif