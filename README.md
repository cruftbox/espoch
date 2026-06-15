# ESPoch

Firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06** smart watch, built with **ESP-IDF**
(C). A from-scratch smartwatch: amber-on-black dashboard watch face, NTP-synced time, step
counting, iPhone notifications/media control over BLE, and more.

## Status

- ✅ **Stage 1 — Hello Watch:** boots, serial console verified.
- ✅ **Stage 2 — Watch Face:** live amber `HH:MM:SS` clock + date + battery/steps fields on the
  AMOLED. (Time is seeded until NTP lands; battery/steps are placeholders for now.)
- ⬜ **Stage 3+:** WiFi/NTP/OTA, step counter, BLE (ANCS + AMS), calendar, voice notes, polish.

See **[PROJECT.md](PROJECT.md)** for the full hardware spec, design, feature roadmap, build &
flash instructions, and ESP-IDF v6 compatibility notes.

## Build & flash (quick)

Open the folder in VS Code with the Espressif IDF extension (ESP-IDF v6.0.1), set target
`esp32s3` and port `COM3`, then click **Build, Flash and Monitor** (UART). Full instructions and
a command-line recipe are in [PROJECT.md](PROJECT.md#build--flash).

## Notes

- The Waveshare BSP is **vendored and patched** for ESP-IDF v6 under
  [`components/esp32_s3_touch_amoled_2_06/`](components/esp32_s3_touch_amoled_2_06/) — see its
  [`PATCHES.md`](components/esp32_s3_touch_amoled_2_06/PATCHES.md).
- `managed_components/` and `build/` are generated and git-ignored; `dependencies.lock` pins
  versions for reproducible builds.

## License

See [LICENSE](LICENSE).
