/*
 * Software pedometer on top of the QMI8658 IMU. See step_counter.h.
 *
 * Algorithm: sample the accelerometer at ~50 Hz, compute the magnitude of the
 * acceleration vector (≈1 g at rest), and count a step on each upward peak that
 * crosses a high threshold, with hysteresis (a low threshold to re-arm) and a
 * minimum time between steps to reject jitter / over-counting. Simple and
 * power-cheap; not lab-accurate, but fine for a daily step total.
 */
#include "step_counter.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "qmi8658.h"

static const char *TAG = "steps";

static volatile int s_steps = 0;

/* Tuning. Magnitudes are in g (1.0 = gravity at rest). */
#define STEP_THRESH_HI   1.15f                 /* peak above this = candidate step */
#define STEP_THRESH_LO   1.05f                 /* must fall below this to re-arm    */
#define STEP_MIN_GAP_MS  250                   /* >= 250 ms apart (max ~4 steps/s)  */
#define STEP_SAMPLE_MS   20                    /* ~50 Hz polling                    */

static void step_task(void *arg)
{
    (void)arg;

    if (qmi8658_init() != ESP_OK || qmi8658_enable_streaming() != ESP_OK) {
        ESP_LOGE(TAG, "IMU init failed — step counting disabled");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "step counter running");

    bool armed = true;                          /* ready to detect the next peak */
    TickType_t last_step = 0;
    const TickType_t min_gap = pdMS_TO_TICKS(STEP_MIN_GAP_MS);

    for (;;) {
        qmi8658_motion_sample_t s;
        if (qmi8658_read_motion(&s) == ESP_OK) {
            float mag = sqrtf(s.accel_g[0] * s.accel_g[0] +
                              s.accel_g[1] * s.accel_g[1] +
                              s.accel_g[2] * s.accel_g[2]);
            TickType_t now = xTaskGetTickCount();

            if (armed && mag > STEP_THRESH_HI && (now - last_step) >= min_gap) {
                s_steps++;
                last_step = now;
                armed = false;
            } else if (!armed && mag < STEP_THRESH_LO) {
                armed = true;                   /* fell back to baseline — ready again */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(STEP_SAMPLE_MS));
    }
}

void step_counter_start(void)
{
    xTaskCreate(step_task, "steps", 4096, NULL, 3, NULL);
}

int step_counter_get(void)
{
    return s_steps;
}

void step_counter_reset(void)
{
    s_steps = 0;
}
