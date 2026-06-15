/**
 * @file watch_display.h
 * @brief Display init using the QSPI IO flush path (not BSP add_disp_rgb).
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission. Fixes the stock BSP's broken RGB flush path on this QSPI AMOLED.
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Start LVGL, panel, touch, and backlight using the QSPI IO flush path.
 * @return LVGL display handle, or NULL on failure.
 */
lv_display_t *watch_display_start(void);

/**
 * @brief Take the LVGL mutex (same as bsp_display_lock).
 * @param timeout_ms Timeout in ms; 0 blocks indefinitely.
 * @return true if the mutex was taken.
 */
bool watch_display_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL mutex (same as bsp_display_unlock).
 */
void watch_display_unlock(void);

/**
 * @brief Set AMOLED brightness (SH8601 command 0x51).
 * @param brightness_percent 0–100.
 * @return ESP_OK on success.
 */
esp_err_t watch_display_set_brightness(int brightness_percent);

/**
 * @brief Touch/pointer input device registered at display start.
 * @return LVGL indev handle, or NULL if touch init failed.
 */
lv_indev_t *watch_display_get_touch_indev(void);
