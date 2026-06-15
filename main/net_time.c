/*
 * WiFi (station) connection + SNTP time sync. See net_time.h.
 *
 * Credentials come from wifi_secrets.h (git-ignored). Timezone is set by the
 * caller (main.c) via the TZ environment variable, so the synced UTC time is
 * displayed as correct local time.
 */
#include "net_time.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "wifi_secrets.h"
#include "rtc_manager.h"

static const char *TAG = "net_time";

#define MAX_RETRIES 20
static int s_retries = 0;
static bool s_sntp_started = false;
static volatile bool s_connected = false;

bool net_time_is_connected(void)
{
    return s_connected;
}

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "system time synced from NTP");
    rtc_manager_write_from_system();   /* persist the accurate time to the hardware RTC */
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retries < MAX_RETRIES) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected; retry %d/%d", s_retries, MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi failed after %d tries — check wifi_secrets.h and that the network is 2.4 GHz", MAX_RETRIES);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_retries = 0;
        s_connected = true;
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&evt->ip_info.ip));
        if (!s_sntp_started) {
            esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            cfg.sync_cb = on_time_sync;
            esp_netif_sntp_init(&cfg);
            s_sntp_started = true;
            ESP_LOGI(TAG, "SNTP started (pool.ntp.org)");
        }
    }
}

void net_time_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started; connecting to \"%s\"", WIFI_SSID);
}
