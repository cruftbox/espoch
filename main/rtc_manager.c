/**
 * @file rtc_manager.c
 * @brief PCF85063 RTC driver wrapper using the Waveshare component and BSP I2C.
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission.
 */

#include "rtc_manager.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "pcf85063a.h"
#include "timezone_manager.h"

static const char *TAG = "rtc";

static pcf85063a_dev_t s_rtc;
static bool s_initialized = false;
static bool s_time_valid = false;

/**
 * @brief Interpret @p tm as UTC and convert to epoch seconds.
 * @param tm Broken-down UTC time.
 * @return Seconds since Unix epoch.
 */
static time_t tm_utc_to_epoch(struct tm *tm)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    return mktime(tm);
}

/**
 * @brief Split epoch seconds into broken-down UTC.
 * @param epoch Seconds since Unix epoch.
 * @param out Output tm struct (UTC).
 */
static void epoch_to_tm_utc(time_t epoch, struct tm *out)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    localtime_r(&epoch, out);
    timezone_manager_apply();
}

/**
 * @brief Convert PCF85063 datetime to a POSIX struct tm.
 * @param dt Source datetime from the chip driver.
 * @param out Destination tm struct.
 */
static void datetime_to_tm(const pcf85063a_datetime_t *dt, struct tm *out)
{
    memset(out, 0, sizeof(*out));
    out->tm_year = (int)dt->year - 1900;
    out->tm_mon = (int)dt->month - 1;
    out->tm_mday = (int)dt->day;
    out->tm_hour = (int)dt->hour;
    out->tm_min = (int)dt->min;
    out->tm_sec = (int)dt->sec;
    out->tm_wday = (int)dt->dotw;
}

/**
 * @brief Convert a POSIX struct tm to PCF85063 datetime.
 * @param tm Source tm struct.
 * @param out Destination chip datetime.
 */
static void tm_to_datetime(const struct tm *tm, pcf85063a_datetime_t *out)
{
    out->year = (uint16_t)(tm->tm_year + 1900);
    out->month = (uint8_t)(tm->tm_mon + 1);
    out->day = (uint8_t)tm->tm_mday;
    out->hour = (uint8_t)tm->tm_hour;
    out->min = (uint8_t)tm->tm_min;
    out->sec = (uint8_t)tm->tm_sec;
    out->dotw = (uint8_t)tm->tm_wday;
}

/**
 * @brief Heuristic check that RTC values look like a real clock setting.
 * @param dt Datetime read from the chip.
 * @return True if year/month/day/hour/minute/second are in range.
 */
static bool datetime_looks_valid(const pcf85063a_datetime_t *dt)
{
    if (dt->year < 2024 || dt->year > 2100) {
        return false;
    }
    if (dt->month < 1 || dt->month > 12) {
        return false;
    }
    if (dt->day < 1 || dt->day > 31) {
        return false;
    }
    if (dt->hour > 23 || dt->min > 59 || dt->sec > 59) {
        return false;
    }
    return true;
}

esp_err_t rtc_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(bsp_i2c_init());

    esp_err_t err = pcf85063a_init(&s_rtc, bsp_i2c_get_handle(), PCF85063A_ADDRESS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063 init failed: %s", esp_err_to_name(err));
        return err;
    }

    pcf85063a_datetime_t now = {0};
    err = pcf85063a_get_time_date(&s_rtc, &now);
    if (err == ESP_OK) {
        s_time_valid = datetime_looks_valid(&now);
        char buf[32];
        pcf85063a_datetime_to_str(buf, now);
        ESP_LOGI(TAG, "RTC reads: %s (%s)", buf, s_time_valid ? "valid" : "invalid");
    } else {
        ESP_LOGW(TAG, "RTC read failed at init: %s", esp_err_to_name(err));
        s_time_valid = false;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t rtc_manager_read(struct tm *out)
{
    if (!s_initialized || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pcf85063a_datetime_t now = {0};
    esp_err_t err = pcf85063a_get_time_date(&s_rtc, &now);
    if (err != ESP_OK) {
        return err;
    }

    datetime_to_tm(&now, out);
    s_time_valid = datetime_looks_valid(&now);
    return ESP_OK;
}

esp_err_t rtc_manager_write(const struct tm *time)
{
    if (!s_initialized || time == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pcf85063a_datetime_t dt = {0};
    tm_to_datetime(time, &dt);

    esp_err_t err = pcf85063a_set_time_date(&s_rtc, dt);
    if (err == ESP_OK) {
        s_time_valid = datetime_looks_valid(&dt);
        ESP_LOGI(TAG, "RTC updated");
    }
    return err;
}

esp_err_t rtc_manager_sync_to_system(void)
{
    struct tm tm_time = {0};
    esp_err_t err = rtc_manager_read(&tm_time);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_time_valid) {
        ESP_LOGW(TAG, "RTC time invalid — not applying to system clock");
        return ESP_ERR_INVALID_STATE;
    }

    struct timeval tv = {
        .tv_sec = tm_utc_to_epoch(&tm_time),
        .tv_usec = 0,
    };
    timezone_manager_apply();

    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "System clock loaded from RTC (UTC)");
    return ESP_OK;
}

esp_err_t rtc_manager_write_from_system(void)
{
    time_t now = time(NULL);
    struct tm tm_time = {0};
    epoch_to_tm_utc(now, &tm_time);
    return rtc_manager_write(&tm_time);
}

bool rtc_manager_is_time_valid(void)
{
    return s_time_valid;
}

esp_err_t rtc_manager_ensure_time_set(void)
{
    if (s_time_valid) {
        return ESP_OK;
    }

    struct tm default_time = {
        .tm_year = 126,
        .tm_mon = 5,
        .tm_mday = 7,
        .tm_hour = 12,
        .tm_min = 0,
        .tm_sec = 0,
        .tm_wday = 0,
    };

    ESP_LOGW(TAG, "RTC unset/invalid — setting default 2026-06-07 12:00:00");
    esp_err_t err = rtc_manager_write(&default_time);
    if (err != ESP_OK) {
        return err;
    }
    return rtc_manager_sync_to_system();
}
