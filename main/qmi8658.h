/**
 * @file qmi8658.h
 * @brief QMI8658 hardware Wake-on-Motion for raise-to-wake.
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Callback invoked on the WoM task when the IMU reports motion. */
typedef void (*qmi8658_motion_cb_t)(void);

/**
 * @brief Probe and reset the QMI8658 on the BSP I2C bus.
 * @return ESP_OK when the chip responds.
 */
esp_err_t qmi8658_init(void);

/**
 * @brief True after successful @ref qmi8658_init.
 */
bool qmi8658_is_ready(void);

/**
 * @brief Enable the chip's built-in Wake-on-Motion on INT1 (GPIO 21).
 * @param on_motion Called from a task when STATUS1 reports a WoM event.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_enable_wake_on_motion(qmi8658_motion_cb_t on_motion);

/**
 * @brief Disable WoM and turn sensors off while the display is awake.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_disable_wake_on_motion(void);

/** @brief One accel + gyro sample for apps (lightsaber, step counter, …). */
typedef struct {
    float accel_g[3];
    float gyro_dps[3];
} qmi8658_motion_sample_t;

/**
 * @brief Enable 6-axis streaming (accel + gyro at 250 Hz). Disables WoM first.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_enable_streaming(void);

/**
 * @brief Stop sensors after streaming (CTRL7 = 0).
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_disable_streaming(void);

/**
 * @brief True when @ref qmi8658_enable_streaming is active.
 */
bool qmi8658_streaming_active(void);

/**
 * @brief Read latest accelerometer and gyroscope values.
 * @param out Filled on success.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_read_motion(qmi8658_motion_sample_t *out);
