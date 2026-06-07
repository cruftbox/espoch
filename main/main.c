/*
 * ESPoch — Stage 1: Hello Watch
 *
 * Boots the ESP32-S3, prints a message to the serial console, and sits idle.
 * This confirms the toolchain, flash, and USB connection all work before any
 * display or peripheral code is added.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "espoch";

void app_main(void)
{
    ESP_LOGI(TAG, "ESPoch booting...");
    ESP_LOGI(TAG, "Stage 1 — Hello Watch");

    while (1) {
        ESP_LOGI(TAG, "alive");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
