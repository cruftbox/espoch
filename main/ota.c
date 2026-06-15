/*
 * OTA firmware updates from GitHub Releases. See ota.h.
 */
#include "ota.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"

#include "net_time.h"

static const char *TAG = "ota";

/* GitHub's "latest release" always resolves to the newest published release, and
 * /download/<asset> serves that release's asset — so this URL self-updates. */
#define OTA_URL "https://github.com/cruftbox/espoch/releases/latest/download/espoch.bin"

static void ota_task(void *arg)
{
    (void)arg;

    /* Wait (up to ~60s) for WiFi. If it never connects, just skip — the watch
     * keeps running its current firmware. */
    for (int i = 0; i < 60 && !net_time_is_connected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!net_time_is_connected()) {
        ESP_LOGW(TAG, "no WiFi — skipping update check");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "checking %s", OTA_URL);

    esp_http_client_config_t http_cfg = {
        .url = OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no update available (begin: %s)", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* Skip the download if the published build matches what we're already running. */
    esp_app_desc_t avail;
    if (esp_https_ota_get_img_desc(handle, &avail) == ESP_OK) {
        const esp_app_desc_t *running = esp_app_get_description();
        ESP_LOGI(TAG, "running '%s', available '%s'", running->version, avail.version);
        if (strncmp(running->version, avail.version, sizeof(avail.version)) == 0) {
            ESP_LOGI(TAG, "already up to date");
            esp_https_ota_abort(handle);
            vTaskDelete(NULL);
            return;
        }
    }

    ESP_LOGI(TAG, "downloading update...");
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        /* keep feeding the download; could drive a progress UI here */
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        vTaskDelete(NULL);
        return;
    }

    err = esp_https_ota_finish(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update installed — rebooting into new firmware");
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

void ota_start(void)
{
    xTaskCreate(ota_task, "ota", 8192, NULL, 4, NULL);
}

void ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "running image confirmed valid (rollback cancelled)");
    }
}
