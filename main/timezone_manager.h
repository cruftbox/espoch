/**
 * @file timezone_manager.h
 * @brief POSIX timezone selection persisted in NVS and applied via TZ/tzset.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** @brief Maximum length of a POSIX TZ string (including NUL). */
#define TIMEZONE_POSIX_MAX 48

/** @brief One selectable timezone entry. */
typedef struct {
    const char *label;
    /** @brief Fixed standard offset (no daylight saving). */
    const char *posix_std;
    /** @brief POSIX string with DST rules, or NULL if DST never applies. */
    const char *posix_dst;
} timezone_entry_t;

/**
 * @brief Load timezone + DST prefs from NVS and apply TZ/tzset.
 * @return ESP_OK on success.
 */
esp_err_t timezone_manager_init(void);

/**
 * @brief Re-apply the in-memory timezone (after temporary TZ overrides).
 */
void timezone_manager_apply(void);

/**
 * @brief Number of built-in timezone entries.
 * @return Entry count for the settings picker.
 */
size_t timezone_manager_entry_count(void);

/**
 * @brief Get a built-in timezone entry by index.
 * @param index Zero-based index.
 * @return Pointer to static entry, or NULL if out of range.
 */
const timezone_entry_t *timezone_manager_get_entry(size_t index);

/**
 * @brief Index of the currently active timezone entry.
 * @return Zero-based index.
 */
size_t timezone_manager_get_selected_index(void);

/**
 * @brief Human-readable label for the active timezone.
 * @return Static label string.
 */
const char *timezone_manager_get_selected_label(void);

/**
 * @brief True when the selected zone supports a DST on/off choice.
 * @param index Zone index, or current selection when @p index is SIZE_MAX.
 * @return True if @p posix_dst is defined for the zone.
 */
bool timezone_manager_supports_dst(size_t index);

/**
 * @brief Whether daylight saving is enabled for the active zone.
 */
bool timezone_manager_dst_enabled(void);

/**
 * @brief Enable or disable DST for the active zone (no-op if unsupported).
 * @param enabled True to use DST rules when available.
 * @return ESP_OK on success.
 */
esp_err_t timezone_manager_set_dst_enabled(bool enabled);

/**
 * @brief Select a built-in timezone, persist to NVS, and apply it.
 * @param index Zero-based entry index.
 * @return ESP_OK on success.
 */
esp_err_t timezone_manager_set_index(size_t index);

/**
 * @brief Copy the active POSIX TZ string into @p buf.
 * @param buf Output buffer.
 * @param buf_len Size of @p buf.
 */
void timezone_manager_get_posix(char *buf, size_t buf_len);

/**
 * @brief Short DST status for the settings toggle.
 * @return "On", "Off", or "N/A".
 */
const char *timezone_manager_dst_label(void);
