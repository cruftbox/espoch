/*
 * Step counter (Stage 4) — uses the QMI8658's built-in hardware pedometer.
 *
 * The chip's vendor-tuned step engine counts steps from a sustained gait
 * pattern (rejecting isolated hand movements), so we just configure it once and
 * read the count register. See qmi8658.c for the register sequence.
 */
#include "step_counter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "qmi8658.h"

static const char *TAG = "steps";

static volatile bool s_ready = false;

/* Bring up the IMU + pedometer off the main path (the CTRL9 config handshakes
 * take a moment). */
static void init_task(void *arg)
{
    (void)arg;

    if (qmi8658_init() == ESP_OK &&
        qmi8658_enable_streaming() == ESP_OK &&
        qmi8658_pedometer_enable() == ESP_OK) {
        s_ready = true;
        ESP_LOGI(TAG, "step counter ready (hardware pedometer)");
    } else {
        ESP_LOGE(TAG, "step counter init failed");
    }
    vTaskDelete(NULL);
}

void step_counter_start(void)
{
    xTaskCreate(init_task, "step_init", 4096, NULL, 3, NULL);
}

int step_counter_get(void)
{
    return s_ready ? (int)qmi8658_pedometer_count() : 0;
}

void step_counter_reset(void)
{
    if (s_ready) {
        qmi8658_pedometer_reset();
    }
}
