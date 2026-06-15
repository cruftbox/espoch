/**
 * @file rtc_manager.h
 * @brief PCF85063 real-time clock — read/write and system clock sync.
 */

#pragma once

#include <stdbool.h>
#include <sys/time.h>
#include <time.h>      /* struct tm */

#include "esp_err.h"

/**
 * @brief Initialize the PCF85063 over the board I2C bus.
 * @return ESP_OK on success.
 */
esp_err_t rtc_manager_init(void);

/**
 * @brief Read the current date/time from the hardware RTC.
 * @param out Filled with RTC time on success.
 * @return ESP_OK on success.
 */
esp_err_t rtc_manager_read(struct tm *out);

/**
 * @brief Write date/time to the hardware RTC (UTC wall time).
 * @param time UTC broken-down time to store in the RTC.
 * @return ESP_OK on success.
 */
esp_err_t rtc_manager_write(const struct tm *time);

/**
 * @brief Load RTC time into the ESP32 system clock (settimeofday).
 * @return ESP_OK if RTC was read and applied.
 */
esp_err_t rtc_manager_sync_to_system(void);

/**
 * @brief Store the current system clock (UTC) into the RTC.
 * @return ESP_OK on success.
 */
esp_err_t rtc_manager_write_from_system(void);

/**
 * @brief Whether the RTC returned a plausible date/time.
 */
bool rtc_manager_is_time_valid(void);

/**
 * @brief If the RTC is unset/invalid, write a default time and sync to the system clock.
 * @return ESP_OK when the clock is usable afterward.
 */
esp_err_t rtc_manager_ensure_time_set(void);
