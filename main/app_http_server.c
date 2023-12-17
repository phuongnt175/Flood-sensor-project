#include "app_http_server.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include <string.h>

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */
extern const uint8_t index_html_start[] asm("_binary_webserver_html_start");
extern const uint8_t index_html_end[] asm("_binary_webserver_html_end");

static const char *TAG = "example";
static httpd_handle_t server = NULL;

static http_post_handle_t http_post_cb = NULL;

static esp_err_t sys_info_handler(httpd_req_t *req)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(deviceInfo.macAddress, sizeof(deviceInfo.macAddress), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get module version
    snprintf(deviceInfo.moduleVersion, sizeof(deviceInfo.moduleVersion), "%s", esp_get_idf_version());

    // Get Wi-Fi mode
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    snprintf(deviceInfo.wifiMode, sizeof(deviceInfo.wifiMode), "%s", (mode == WIFI_MODE_AP) ? "AP" : "Station");

    // Get AP SSID and IP (assuming in AP mode)
    if (mode == WIFI_MODE_AP) {
        wifi_config_t ap_config;
        esp_wifi_get_config(WIFI_IF_AP, &ap_config);
        snprintf(deviceInfo.apSsid, sizeof(deviceInfo.apSsid), "%s", (char *)ap_config.ap.ssid);
        tcpip_adapter_ip_info_t ap_ip_info;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ap_ip_info);
        snprintf(deviceInfo.apIp, sizeof(deviceInfo.apIp), IPSTR, IP2STR(&ap_ip_info.ip));
    }

    // Create JSON response
    char json_response[1024];
    snprintf(json_response, sizeof(json_response),
             "{"
             "\"macAddress\":\"%s\","
             "\"moduleVersion\":\"%s\","
             "\"wifiMode\":\"%s\","
             "\"apSsid\":\"%s\","
             "\"apIp\":\"%s\""
             "}",
             deviceInfo.macAddress, deviceInfo.moduleVersion, deviceInfo.wifiMode,
             deviceInfo.apSsid, deviceInfo.apIp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    return ESP_OK;
}

static const httpd_uri_t sys_info = {
    .uri = "/sysinfo",
    .method = HTTP_GET,
    .handler = sys_info_handler,
    .user_ctx = "hello word"
};

/* An HTTP GET handler */
static esp_err_t root_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    httpd_resp_set_type(req, "text/html");
    const char *resp_str = (const char *)index_html_start;
    httpd_resp_send(req, resp_str, index_html_end - index_html_start);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/index",
    .method = HTTP_GET,
    .handler = root_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "Hello World!"};

/* An HTTP POST handler */
static esp_err_t http_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, data_len = req->content_len;

    /* Read the data for the request */
    httpd_req_recv(req, buf, data_len);
    /* Log data received */
    ESP_LOGI(TAG, "Data recv: %.*s", data_len, buf);
    http_post_cb(buf, data_len);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t http_post = {
    .uri = "/post",
    .method = HTTP_POST,
    .handler = http_post_handler,
    .user_ctx = NULL};

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &http_post);
        httpd_register_uri_handler(server, &sys_info);
    }
}

void stop_webserver(void)
{
    httpd_stop(server);
}

void http_post_set_callback(void *cb)
{
    if (cb)
    {
        http_post_cb = cb;
    }
}