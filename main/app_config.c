#include "app_config.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

#include "lwip/err.h" 
#include "lwip/sys.h"
#include <lwip/ip_addr.h>


#include "app_http_server.h"
#include "cJSON.h"


// NVS namespace for AP configuration
#define NVS_NAMESPACE "ap_config"
#define NVS_APMODE "provision_type"

// NVS keys for AP configuration
#define NVS_KEY_AP_SSID     "ap_ssid"
#define NVS_KEY_AP_PASSWORD "ap_password"
#define NVS_KEY_AP_CHANNEL  "ap_channel"

#define NVS_KEY_PROVISIONTYPE "pt"

static char * TAG = "CONFIG_WIFI";

// provision_type_t provision_type = PROVISION_SMARTCONFIG; // smart config ok rồi
provision_type_t provision_type = PROVISION_ACCESSPOINT; 

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int HTTP_CONFIG_DONE = BIT2;

char wifiMode[10] = {0};
char apSsid[65] = {0};
char apPass[65] = {0};
char apChan[10] = {0};
char staSsid[65] = {0};
char staPass[65] = {0};
char dhcp[10] = {0};
char ipAddress[20] = {0};
char subnetMask[20] = {0};
char Gateway[20] = {0};
char DNS[20] = {0};

esp_err_t set_sta_static_ip(const char *ip, const char *net_mask, const char *gate_way) {
    tcpip_adapter_ip_info_t ip_info;

    // Parse IP address, netmask, gateway, and DNS
    ip4addr_aton(ip, &ip_info.ip);
    ip4addr_aton(net_mask, &ip_info.netmask);
    ip4addr_aton(gate_way, &ip_info.gw);

    // Stop DHCP client
    esp_err_t ret = tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
    if (ret != ESP_OK) {
        printf("Failed to stop DHCP client\n");
        return ret;
    }

    // Set static IP configuration
    ret = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
    if (ret != ESP_OK) {
        printf("Failed to set static IP configuration\n");
        return ret;
    }

    //set netmask configuration

    printf("STA IP configuration set successfully\n");
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
         esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
         ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } 

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
    ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                MAC2STR(event->mac), event->aid);
    }

    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

// Function to initialize NVS
void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// Function to save AP configuration to NVS
void save_ap_config(const char *ssid, const char *password, const char *channel) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
        return;
    }

    // Save AP configuration to NVS
    err = nvs_set_str(nvs_handle, NVS_KEY_AP_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving AP SSID", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "save new AP SSID config");

    err = nvs_set_str(nvs_handle, NVS_KEY_AP_PASSWORD, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving AP password", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "save new AP password config");

    err = nvs_set_str(nvs_handle, NVS_KEY_AP_CHANNEL, channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving AP channel", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "save new AP channel config");

    // Commit changes to NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing changes to NVS", esp_err_to_name(err));
    }

    // Close NVS handle
    nvs_close(nvs_handle);
}

// Function to load AP configuration from NVS
void load_ap_config(char *ssid, char *password, char *channel) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
        return;
    }

    // Read AP configuration from NVS
    size_t required_size = sizeof(ap_config_t);
    ap_config_t ap_config;
    err = nvs_get_str(nvs_handle, NVS_KEY_AP_SSID, ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading AP SSID", esp_err_to_name(err));
    }

    required_size = sizeof(ap_config.password);
    err = nvs_get_str(nvs_handle, NVS_KEY_AP_PASSWORD, password, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading AP password", esp_err_to_name(err));
    }

    required_size = sizeof(ap_config.channel);
    err = nvs_get_str(nvs_handle, NVS_KEY_AP_CHANNEL, channel, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading AP channel", esp_err_to_name(err));
    }

    // Close NVS handle
    nvs_close(nvs_handle);
}

void save_provision_type() {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    ESP_ERROR_CHECK(err);

    // Write provision_type to NVS
    err = nvs_set_u8(my_handle, "provision_type", provision_type);
    ESP_ERROR_CHECK(err);

    // Commit the changes to flash
    err = nvs_commit(my_handle);
    ESP_ERROR_CHECK(err);

    // Close NVS
    nvs_close(my_handle);
}

// Function to load provision_type from NVS
void load_provision_type() {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        // If NVS partition is not initialized yet, initialize it
        init_nvs();
    }

    // Read provision_type from NVS
    err = nvs_get_u8(my_handle, "provision_type", (uint8_t*)&provision_type);
    if (err != ESP_OK) {
        // If provision_type key is not found, use the default value
        provision_type = PROVISION_ACCESSPOINT;
    }

    // Close NVS
    nvs_close(my_handle);
}

bool is_provisioned(void)
{
    bool provisioned = false;
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config;
    esp_wifi_get_config(WIFI_IF_STA, &wifi_config); // get wifi

    if (wifi_config.sta.ssid[0] != 0x00) // kiểm tra xem có wifi hay k
    {
        provisioned = true;
    }
    load_provision_type();
    return provisioned;
}

// Function to remove NULL characters from a string
void remove_null_characters(char *str) {
    int len = strlen(str);
    for (int i = 0; i < len; ++i) {
        if (str[i] == '\0') {
            for (int j = i; j < len; ++j) {
                str[j] = str[j + 1];
            }
            --len;
            --i;  // Check the current index again as the character at this index has changed
        }
    }
}

void ap_start(void)
{
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "webserver_esp32",
            .ssid_len = strlen((char*)"webserver_esp32"),
            .channel = 1,
            .password = "88888888",  // mật khẩu ngắn quá thì cũng bị reset
            .max_connection = 4,  // được 4 đứa kết nối vào
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (wifi_config.ap.password[0] == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void ap_webserver_start(void)
{
    ESP_LOGE(TAG, "enter ap webserver start");
    load_ap_config(apSsid, apPass, apChan);
    ESP_LOGI(TAG, "ssid: %s", apSsid);
    ESP_LOGI(TAG, "pass: %s", apPass);
    ESP_LOGI(TAG, "channel: %s", apChan);
    ESP_LOGE(TAG, "load new ap setting");
    wifi_config_t wifi_config;

    strncpy((char *)wifi_config.ap.ssid, apSsid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';

    strncpy((char *)wifi_config.ap.password, apPass, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';

    if (strlen(apSsid) == 0 || strlen(apPass) == 0) {
        // Use default settings
        provision_type = PROVISION_ACCESSPOINT;
        save_provision_type();
        esp_restart();
    }
    wifi_config.ap.channel = atoi(apChan);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (wifi_config.ap.password[0] == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGE(TAG, "start new ap");
}

void http_post_data_callback (char *buf, int len)
{
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        // Handle parsing error
        printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return;
    }

    // Extract values from JSON
    strcpy(wifiMode, cJSON_GetObjectItem(json, "wifiMode")->valuestring);
    strcpy(dhcp, cJSON_GetObjectItem(json, "dhcp")->valuestring);

    // Clean up cJSON object
    

    // Handle different configurations based on wifiMode and dhcp
    if (strcmp(wifiMode, "ap") == 0) {
        strcpy(apSsid, cJSON_GetObjectItem(json, "ssid")->valuestring);
        strcpy(apPass, cJSON_GetObjectItem(json, "password")->valuestring);
        strcpy(apChan, cJSON_GetObjectItem(json, "channel")->valuestring);

        printf("apSsid: %s\n", apSsid);
        printf("apPass: %s\n", apPass);
        printf("apChan: %s\n", apChan);

        save_ap_config(apSsid, apPass, apChan);
        xEventGroupSetBits(s_wifi_event_group, HTTP_CONFIG_DONE);
    } else if (strcmp(wifiMode, "station") == 0) {
        strcpy(staSsid, cJSON_GetObjectItem(json, "ssid")->valuestring);
        strcpy(staPass, cJSON_GetObjectItem(json, "password")->valuestring);
        printf("staSsid: %s\n", staSsid);
        printf("staPass: %s\n", staPass);

        if (strcmp(dhcp, "false") == 0) {
            strcpy(ipAddress, cJSON_GetObjectItem(json, "ipAddress")->valuestring);
            strcpy(subnetMask, cJSON_GetObjectItem(json, "subnetMask")->valuestring);
            strcpy(Gateway, cJSON_GetObjectItem(json, "gateway")->valuestring);
            strcpy(DNS, cJSON_GetObjectItem(json, "dns")->valuestring);
            printf("ipAddress: %s\n", ipAddress);
            printf("subnetMask: %s\n", subnetMask);
            printf("Gateway: %s\n", Gateway);
            printf("DNS: %s\n", DNS);
        }
    }
    cJSON_Delete(json);
    xEventGroupSetBits(s_wifi_event_group, HTTP_CONFIG_DONE);
}

void app_config(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    s_wifi_event_group = xEventGroupCreate();
    bool provisioned = is_provisioned();
    if (!provisioned)
    {
        ESP_LOGE(TAG, "chua co mang wifi");
        if (provision_type == PROVISION_SMARTCONFIG)
        {
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
            smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
            ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
            xEventGroupWaitBits(s_wifi_event_group , ESPTOUCH_DONE_BIT, false, true, portMAX_DELAY); 
            esp_smartconfig_stop();
          
        }
        else if (provision_type == PROVISION_ACCESSPOINT)
        {
            ap_start();
            start_webserver();
            http_post_set_callback(http_post_data_callback);
            xEventGroupWaitBits(s_wifi_event_group , HTTP_CONFIG_DONE, false, true, portMAX_DELAY); 

            // convert station mode and connect router
            stop_webserver();

            wifi_config_t wifi_config;
            bzero(&wifi_config, sizeof(wifi_config_t));
            if (strcmp(wifiMode, "station") == 0)
            {
                memcpy(wifi_config.sta.ssid, staSsid, strlen(staSsid));
                memcpy(wifi_config.sta.password, staPass, strlen(staPass));
                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                ESP_ERROR_CHECK(esp_wifi_init(&cfg));
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_start());
                esp_restart();
            }
            else if(strcmp(wifiMode, "ap") == 0)
            {
                esp_wifi_restore();
                provision_type = PROVISION_AP;
                save_provision_type();
                esp_restart();
            }
        }
        else if(provision_type == PROVISION_AP)
        {
            ap_webserver_start();
            start_webserver();
            http_post_set_callback(http_post_data_callback);
            xEventGroupWaitBits(s_wifi_event_group , HTTP_CONFIG_DONE, false, true, portMAX_DELAY); 

            // convert station mode and connect router
            stop_webserver();

            wifi_config_t wifi_config;
            bzero(&wifi_config, sizeof(wifi_config_t));
            if (strcmp(wifiMode, "station") == 0)
            {
                memcpy(wifi_config.sta.ssid, staSsid, strlen(staSsid));
                memcpy(wifi_config.sta.password, staPass, strlen(staPass));
                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                ESP_ERROR_CHECK(esp_wifi_init(&cfg));
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_start());
                esp_restart();
            }
            else if(strcmp(wifiMode, "ap") == 0)
            {
                esp_wifi_restore();
                provision_type = PROVISION_AP;
                save_provision_type();
                esp_restart();
            }
        }
    }
    else
    {   
        //esp_wifi_restore();
        ESP_ERROR_CHECK(esp_wifi_start());
        tcpip_adapter_ip_info_t ip_info;

        // Get the IP address of the STA interface
        esp_err_t ret = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        if (ret == ESP_OK) {
            // Print or use the IP address
            printf("STA IP Address: %s\n", ip4addr_ntoa(&ip_info.ip));
        } else {
            printf("Failed to get STA IP address\n");
        }
        start_webserver();
        http_post_set_callback(http_post_data_callback);
        xEventGroupWaitBits(s_wifi_event_group , HTTP_CONFIG_DONE, false, true, portMAX_DELAY); 
        // convert station mode and connect router
        stop_webserver();

        wifi_config_t wifi_config;
        bzero(&wifi_config, sizeof(wifi_config_t));
        if (strcmp(wifiMode, "station") == 0)
        {
            memcpy(wifi_config.sta.ssid, staSsid, strlen(staSsid));
            memcpy(wifi_config.sta.password, staPass, strlen(staPass));
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            esp_restart();
        }
        else if(strcmp(wifiMode, "ap") == 0)
        {
            esp_wifi_restore();
            provision_type = PROVISION_AP;
            save_provision_type();
            esp_restart();
        }
    }
    xEventGroupWaitBits(s_wifi_event_group , WIFI_CONNECTED_BIT, false, true, portMAX_DELAY); 
    ESP_LOGI(TAG, "wifi connected");
}