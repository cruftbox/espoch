# Code borrowed from `watch-os`

ESPoch reuses code from **Ben Brown's `watch-os`** (https://github.com/benbrown/watch-os),
which targets the same Waveshare ESP32-S3 AMOLED hardware. Ben gave permission to use what we
need. Borrowed files carry an attribution note in their header and are adapted for our stack
(ESP-IDF 6.0 / LVGL 9.5; his targets IDF 5.4 / LVGL 9.2).

## Adopted

| ESPoch file(s) | From watch-os | Purpose | Adopted |
| --- | --- | --- | --- |
| `main/watch_display.c/.h` | `main/display/watch_display.*` | QSPI IO display path — fixes the BSP's broken RGB flush (ghosting / underflow) | Stage 3 |
| `main/rtc_manager.c/.h` | `main/time/rtc_manager.*` | PCF85063 hardware RTC — time survives reboots | Stage 3 |
| `main/timezone_manager.c/.h` | `main/time/timezone_manager.*` | POSIX timezone table persisted in NVS | Stage 3 |

## Planned / candidates (backlog — don't forget)

This is the running list of code we want to pull in during later phases. Move a row up to
**Adopted** as we integrate it.

| watch-os file(s) | Purpose | For ESPoch stage |
| --- | --- | --- |
| `net/wifi_manager.*`, `net/wifi_provision.*`, `apps/wifi_menu.*` | On-device WiFi setup (scan/connect, saved creds, UI) — retires hardcoded `wifi_secrets.h` | Stage 3, **needs app/menu framework** |
| `time/ntp_sync.*` | Manual/periodic NTP→RTC sync | Stage 3 (compare with our `net_time.c`) |
| `sensors/qmi8658.*` | IMU step counting | Stage 4 |
| `power/battery_manager.*`, `power/power_manager.*` | Battery + sleep/wake/screen-off | Stage 8 |
| `input/boot_button.*` | Button handling (PWR/BOOT press map) | Stage 5 |
| `net/ble_scanner.*` | NimBLE plumbing reference | Stage 5 (ANCS/AMS) |
| `fonts/typo_digit_192/96/48` | Big custom clock digits | Visual upgrade, anytime |
| App/menu framework (`apps/*`, menu system) | Multi-screen navigation | **Wanted eventually** — unlocks wifi_menu, settings, calendar, notif history |
| `net/network_task.*`, `net/watch_queues.*` | Off-UI-thread networking with queues | Stage 7 (upload queue) |
| Bonus apps: `stopwatch`, `moon_view`/`moon_phase`, `rf_scanner`, `lightsaber_*` | Extra watch apps | Optional |

> Note: watch-os's `partitions.csv` is single-app (no OTA slots), so ESPoch's OTA support
> (Stage 3) is built independently, not borrowed.

## Third-party components vendored & patched for ESP-IDF v6

Like the BSP, the `waveshare/pcf85063a` RTC driver doesn't build on v6 (its `REQUIRES` lists only
the legacy `driver`, but v6 split `driver/i2c_master.h` into `esp_driver_i2c`). It is **vendored
and patched** at `components/pcf85063a` (added `esp_driver_i2c` to `REQUIRES`) — permanent and
committed, so it survives a `managed_components/` wipe.
