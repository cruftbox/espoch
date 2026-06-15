/**
 * @file timezone_manager.c
 * @brief POSIX timezone storage (NVS) and libc TZ application.
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission.
 */

#include "timezone_manager.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "tz_mgr";
static const char *NVS_NAMESPACE = "time";
static const char *NVS_KEY_IDX = "tz_idx";
static const char *NVS_KEY_DST = "dst_on";
static const char *NVS_KEY_POSIX_LEGACY = "posix";

/** @brief Built-in zones — POSIX strings per ESP-IDF/newlib TZ format. */
static const timezone_entry_t kTimezones[] = {
    {"UTC", "UTC0", NULL},
    {"US Eastern", "EST5", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Pacific", "PST8", "PST8PDT,M3.2.0,M11.1.0"},
    {"UK", "GMT0", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1", "CET-1CEST,M3.5.0,M10.5.0"},
    {"Japan", "JST-9", NULL},
    {"China", "CST-8", NULL},
    {"India", "IST-5:30", NULL},
    {"Australia Eastern", "AEST-10", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"New Zealand", "NZST-12", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

static char s_active_posix[TIMEZONE_POSIX_MAX] = "UTC0";
static size_t s_selected_index;
static bool s_dst_enabled = true;

/**
 * @brief Resolve the POSIX string for a zone and DST preference.
 * @param index Zone index.
 * @param dst_enabled True to use DST rules when available.
 * @return POSIX TZ string.
 */
static const char *posix_for_zone(size_t index, bool dst_enabled)
{
    const timezone_entry_t *entry = timezone_manager_get_entry(index);
    if (entry == NULL) {
        return "UTC0";
    }
    if (dst_enabled && entry->posix_dst != NULL) {
        return entry->posix_dst;
    }
    return entry->posix_std;
}

/**
 * @brief Apply zone index + DST flag to libc.
 * @param index Zone index.
 * @param dst_enabled DST preference.
 */
static void apply_zone(size_t index, bool dst_enabled)
{
    const char *posix = posix_for_zone(index, dst_enabled);
    strncpy(s_active_posix, posix, sizeof(s_active_posix) - 1);
    s_active_posix[sizeof(s_active_posix) - 1] = '\0';
    setenv("TZ", s_active_posix, 1);
    tzset();
}

/**
 * @brief Persist zone index and DST flag to NVS.
 * @return ESP_OK on success.
 */
static esp_err_t save_prefs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_IDX, (uint8_t)s_selected_index);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_DST, s_dst_enabled ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief Infer zone index from a legacy stored POSIX string.
 * @param posix Previously stored TZ string.
 * @param out_dst Set from whether @p posix includes DST rules.
 * @return Zone index, or 0 if unknown.
 */
static size_t legacy_index_from_posix(const char *posix, bool *out_dst)
{
    if (posix == NULL || posix[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; i < timezone_manager_entry_count(); ++i) {
        const timezone_entry_t *entry = &kTimezones[i];
        if (strcmp(posix, entry->posix_std) == 0) {
            if (out_dst != NULL) {
                *out_dst = false;
            }
            return i;
        }
        if (entry->posix_dst != NULL && strcmp(posix, entry->posix_dst) == 0) {
            if (out_dst != NULL) {
                *out_dst = true;
            }
            return i;
        }
    }
    return 0;
}

esp_err_t timezone_manager_init(void)
{
    nvs_handle_t handle;
    uint8_t idx = 0;
    uint8_t dst = 1;
    bool loaded = false;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, NVS_KEY_IDX, &idx) == ESP_OK) {
            nvs_get_u8(handle, NVS_KEY_DST, &dst);
            if (idx < timezone_manager_entry_count()) {
                s_selected_index = idx;
                s_dst_enabled = (dst != 0);
                loaded = true;
            }
        } else {
            char legacy[TIMEZONE_POSIX_MAX] = {0};
            size_t len = sizeof(legacy);
            if (nvs_get_str(handle, NVS_KEY_POSIX_LEGACY, legacy, &len) == ESP_OK && legacy[0] != '\0') {
                bool legacy_dst = true;
                s_selected_index = legacy_index_from_posix(legacy, &legacy_dst);
                s_dst_enabled = legacy_dst;
                loaded = true;
            }
        }
        nvs_close(handle);
    }

    if (!loaded) {
        s_selected_index = 0;
        s_dst_enabled = true;
    }

    if (!timezone_manager_supports_dst(s_selected_index)) {
        s_dst_enabled = false;
    }

    apply_zone(s_selected_index, s_dst_enabled);
    ESP_LOGI(TAG, "Timezone: %s, DST %s (%s)",
             timezone_manager_get_selected_label(),
             timezone_manager_dst_label(),
             s_active_posix);
    return save_prefs();
}

void timezone_manager_apply(void)
{
    setenv("TZ", s_active_posix, 1);
    tzset();
}

size_t timezone_manager_entry_count(void)
{
    return sizeof(kTimezones) / sizeof(kTimezones[0]);
}

const timezone_entry_t *timezone_manager_get_entry(size_t index)
{
    if (index >= timezone_manager_entry_count()) {
        return NULL;
    }
    return &kTimezones[index];
}

size_t timezone_manager_get_selected_index(void)
{
    return s_selected_index;
}

const char *timezone_manager_get_selected_label(void)
{
    const timezone_entry_t *entry = timezone_manager_get_entry(s_selected_index);
    return (entry != NULL) ? entry->label : "UTC";
}

bool timezone_manager_supports_dst(size_t index)
{
    if (index == SIZE_MAX) {
        index = s_selected_index;
    }
    const timezone_entry_t *entry = timezone_manager_get_entry(index);
    return (entry != NULL && entry->posix_dst != NULL);
}

bool timezone_manager_dst_enabled(void)
{
    return s_dst_enabled && timezone_manager_supports_dst(s_selected_index);
}

esp_err_t timezone_manager_set_dst_enabled(bool enabled)
{
    if (!timezone_manager_supports_dst(s_selected_index)) {
        s_dst_enabled = false;
        return ESP_OK;
    }

    s_dst_enabled = enabled;
    apply_zone(s_selected_index, s_dst_enabled);

    esp_err_t err = save_prefs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save DST pref: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DST %s for %s", timezone_manager_dst_label(), timezone_manager_get_selected_label());
    return ESP_OK;
}

esp_err_t timezone_manager_set_index(size_t index)
{
    const timezone_entry_t *entry = timezone_manager_get_entry(index);
    if (entry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_selected_index = index;
    if (!timezone_manager_supports_dst(index)) {
        s_dst_enabled = false;
    }

    apply_zone(s_selected_index, s_dst_enabled);

    esp_err_t err = save_prefs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save timezone: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Timezone set to %s, DST %s", entry->label, timezone_manager_dst_label());
    return ESP_OK;
}

void timezone_manager_get_posix(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    strncpy(buf, s_active_posix, buf_len - 1);
    buf[buf_len - 1] = '\0';
}

const char *timezone_manager_dst_label(void)
{
    if (!timezone_manager_supports_dst(s_selected_index)) {
        return "N/A";
    }
    return s_dst_enabled ? "On" : "Off";
}
