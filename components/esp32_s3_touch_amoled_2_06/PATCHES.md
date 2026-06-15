# Local patches to the Waveshare BSP

This is a **vendored, modified copy** of the Espressif Component Registry package
`waveshare/esp32_s3_touch_amoled_2_06` **v1.0.7**.

It lives here (in the project's `components/` folder) instead of being pulled as a managed
dependency because the published version does not build on **ESP-IDF v6.0.1** and needs the
fixes below. Keeping it local makes the fixes permanent and version-controlled — a fresh
download from the registry would lose them.

## Why each change

ESP-IDF v6 made breaking changes the BSP (written for v5) was not updated for:

- The monolithic `driver` component was split into `esp_driver_*` pieces.
- GCC was updated to 15, which is stricter about C (empty `()` parameter lists, attributes).
- The SPI master driver now aborts transfers on DMA TX underflow instead of tolerating it.

## Changes vs upstream v1.0.7

### 1. `CMakeLists.txt` — added split driver components to `REQUIRES`

```
- REQUIRES driver esp_driver_i2c esp_driver_gpio esp_lcd
+ REQUIRES driver esp_driver_i2c esp_driver_gpio esp_lcd esp_driver_ledc esp_driver_sdmmc esp_driver_i2s
```

The source includes `driver/ledc.h`, `driver/sdmmc_host.h`, and `driver/i2s_std.h`, which in
v6 live in `esp_driver_ledc` / `esp_driver_sdmmc` / `esp_driver_i2s`.

### 2. `esp32_s3_touch_amoled_2_06.c` — fixed `bsp_display_lcd_init` call

`bsp_display_lcd_init` is **defined** with no parameters but was **called** with `cfg`
(which it ignores). Harmless under old compilers; a hard error under GCC 15.

```
- BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);
+ BSP_NULL_CHECK(disp = bsp_display_lcd_init(), NULL);
```

### 3. `esp32_s3_touch_amoled_2_06.c` — lowered QSPI pixel clock to 20 MHz

The `SH8601_PANEL_IO_QSPI_CONFIG` macro defaults to 40 MHz. At 40 MHz, ESP-IDF v6's SPI driver
aborts the display flush with `DMA TX underflow detected`, leaving the screen showing garbage.
20 MHz transfers reliably and is far more than a watch face needs.

```
- const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
+ esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
+ io_config.pclk_hz = 20 * 1000 * 1000;
```

## Related project-level changes (outside this folder)

- `main/idf_component.yml` — adds `espressif/usb` (v6 moved it to the registry) and pins
  `lvgl/lvgl >=9.3` (the LVGL port needs a 9.3+ color constant).
- `sdkconfig.defaults` — PSRAM XIP + 64-byte data cache line + perf optimization (display DMA
  bandwidth); LVGL fonts; `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` left **off** (GCC 15 error).

## Upstream reference

- Component: https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_06
- Source: https://github.com/waveshareteam/Waveshare-ESP32-components (path `bsp/esp32_s3_touch_amoled_2_06`)
- Examples: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
