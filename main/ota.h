/*
 * Over-the-air firmware updates from GitHub Releases (Stage 3).
 *
 * On boot (after WiFi) the watch checks the repo's "latest" release for a build
 * whose version differs from the running one, and if found downloads it into the
 * spare OTA slot and reboots into it. Download is over HTTPS, verified with the
 * ESP-IDF root-CA bundle (no pinned certs).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the background OTA task (waits for WiFi, checks for an update). */
void ota_start(void);

/* Mark the running image as good so the bootloader won't roll it back. Call once
 * the watch has booted far enough to be considered healthy. */
void ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
