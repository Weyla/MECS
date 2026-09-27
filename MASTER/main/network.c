/* Station-mode Wi-Fi setup. Credentials are provisioned through USB serial and
 * stored by ESP-IDF in NVS. CAN ownership never crosses into Wi-Fi event callbacks. */
#include "master.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

static esp_netif_t *station;
static atomic_bool configured;

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (configured) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Driver connection attempts include scan/authentication delays. This
         * keeps retrying without blocking the CAN owner task. */
        if (configured) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        master_note("Web interface: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

void network_start(void)
{
    /* Do not silently erase NVS on an initialization error: it may contain
     * the user's Wi-Fi settings. Report the error through ESP_ERROR_CHECK. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    station = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(station ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t saved = {0};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &saved));
    configured = saved.sta.ssid[0] != 0;
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    if (!configured) {
        master_note("Set Wi-Fi over USB: wifi SSID|PASSWORD");
    }
}

bool network_credentials(const char *ssid, const char *password)
{
    size_t a = strlen(ssid), b = strlen(password);
    /* WPA/WPA2 passphrases, or an empty password for an open test network.
     * Full 32-byte SSIDs are copied as bytes; a terminator is not required. */
    if (a < 1 || a > 32 || b > 63 || (b > 0 && b < 8)) {
        return false;
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, a);
    memcpy(config.sta.password, password, b);
    configured = false;
    esp_wifi_disconnect();
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    if (error != ESP_OK) {
        return false;
    }
    configured = true;
    return esp_wifi_connect() == ESP_OK;
}

void network_address(char *buffer, size_t size)
{
    esp_netif_ip_info_t info = {0};
    if (station && esp_netif_get_ip_info(station, &info) == ESP_OK && info.ip.addr) {
        snprintf(buffer, size, IPSTR, IP2STR(&info.ip));
    } else {
        snprintf(buffer, size, "not connected");
    }
}
