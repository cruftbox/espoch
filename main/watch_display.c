/**
 * @file watch_display.c
 * @brief Display init for the Waveshare AMOLED watch.
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission. Ported to ESP-IDF 6.0 / LVGL 9.5 for ESPoch (compiled unchanged).
 *
 * The stock BSP calls lvgl_port_add_disp_rgb() for this QSPI panel. That marks
 * the display as RGB and calls lv_disp_flush_ready() before SPI finishes, so
 * multi-strip redraws overflow the SPI queue (garbled pixels, ghosting).
 * We use lvgl_port_add_disp() (IO path), a deeper SPI transaction queue, flush
 * retries when the queue is full, and tx_color error checking (SH8601 draw_bitmap
 * ignores failures).
 */

#include "watch_display.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "draw/sw/lv_draw_sw.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_sh8601.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watch_disp";

/** @brief Panel column offset from set_gap(0x16, 0). */
static const int kPanelXGap = 0x16;

/** @brief Shallow queue — each slot may need an internal DMA staging copy (Espressif LCD note). */
static const int kSpiTransQueueDepth = 2;

/** @brief Flush retries when spi_device_queue_trans returns ESP_ERR_NO_MEM. */
static const int kFlushMaxRetries = 40;

static esp_lcd_panel_io_handle_t s_io_handle;

/** @brief Pointer/touch indev registered with LVGL (used for idle input policy). */
static lv_indev_t *s_touch_indev;

/** @brief QSPI write opcodes (must match esp_lcd_sh8601.c). */
static const uint32_t kLcdOpcodeWriteCmd = 0x02U;
static const uint32_t kLcdOpcodeWriteColor = 0x32U;

/** @brief Must match Waveshare BSP lcd_init_cmds. */
static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

/**
 * @brief SH8601 requires flush coordinates aligned to 2 pixels (LVGL 9 event).
 * @param e LVGL invalidate-area event.
 */
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (area == NULL) {
        return;
    }

    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

/**
 * @brief Set panel brightness via command 0x51 (same encoding as BSP).
 * @param brightness_percent 0–100.
 * @return ESP_OK on success.
 */
esp_err_t watch_display_set_brightness(int brightness_percent)
{
    if (s_io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (brightness_percent < 0 || brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t lcd_cmd = 0x51U;
    lcd_cmd &= 0xffU;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02U << 24;
    const uint8_t param = (uint8_t)(brightness_percent * 255 / 100);
    return esp_lcd_panel_io_tx_param(s_io_handle, (int)lcd_cmd, &param, 1);
}

/**
 * @brief Send a QSPI panel command (CASET / RASET).
 * @param io Panel IO handle.
 * @param lcd_cmd 8-bit command index.
 * @param param Command payload.
 * @param param_size Payload length in bytes.
 * @return ESP_OK when queued successfully.
 */
static esp_err_t watch_panel_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param,
                                      size_t param_size)
{
    uint32_t cmd = (uint32_t)lcd_cmd;
    cmd &= 0xffU;
    cmd <<= 8;
    cmd |= kLcdOpcodeWriteCmd << 24;
    return esp_lcd_panel_io_tx_param(io, (int)cmd, param, param_size);
}

/**
 * @brief Send RGB565 pixels over QSPI with error propagation.
 * @param io Panel IO handle.
 * @param color_data Pixel buffer.
 * @param color_size Length in bytes.
 * @return ESP_OK when queued successfully.
 */
static esp_err_t watch_panel_tx_color(esp_lcd_panel_io_handle_t io, const void *color_data, size_t color_size)
{
    uint32_t cmd = (uint32_t)LCD_CMD_RAMWR;
    cmd &= 0xffU;
    cmd <<= 8;
    cmd |= kLcdOpcodeWriteColor << 24;
    return esp_lcd_panel_io_tx_color(io, (int)cmd, color_data, color_size);
}

/**
 * @brief Draw a bitmap region, propagating SPI queue failures to the caller.
 * @param io Panel IO handle.
 * @param x_start First column (LVGL coordinates).
 * @param y_start First row.
 * @param x_end One past last column.
 * @param y_end One past last row.
 * @param color_data RGB565 buffer.
 * @return ESP_OK when the color transfer was queued.
 */
static esp_err_t watch_panel_draw_bitmap(esp_lcd_panel_io_handle_t io, int x_start, int y_start, int x_end,
                                         int y_end, const void *color_data)
{
    x_start += kPanelXGap;
    x_end += kPanelXGap;

    const uint8_t caset[] = {
        (uint8_t)((x_start >> 8) & 0xFF),
        (uint8_t)(x_start & 0xFF),
        (uint8_t)(((x_end - 1) >> 8) & 0xFF),
        (uint8_t)((x_end - 1) & 0xFF),
    };
    esp_err_t err = watch_panel_tx_param(io, LCD_CMD_CASET, caset, sizeof(caset));
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t raset[] = {
        (uint8_t)((y_start >> 8) & 0xFF),
        (uint8_t)(y_start & 0xFF),
        (uint8_t)(((y_end - 1) >> 8) & 0xFF),
        (uint8_t)((y_end - 1) & 0xFF),
    };
    err = watch_panel_tx_param(io, LCD_CMD_RASET, raset, sizeof(raset));
    if (err != ESP_OK) {
        return err;
    }

    const size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * 2U;
    return watch_panel_tx_color(io, color_data, len);
}

/**
 * @brief LVGL flush — IO path; retry when SPI queue is full before giving up.
 * @param drv LVGL display handle.
 * @param area Dirty region.
 * @param color_map RGB565 pixel buffer for @p area.
 */
static void watch_flush_cb(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
    const size_t len = lv_area_get_size(area);
    lv_draw_sw_rgb565_swap(color_map, len);

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < kFlushMaxRetries; ++attempt) {
        err = watch_panel_draw_bitmap(s_io_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                                      color_map);
        if (err == ESP_OK) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGE(TAG, "flush failed after %d retries (%s) — holding LVGL until IO done", kFlushMaxRetries,
             esp_err_to_name(err));
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const size_t strip_bytes = (size_t)(area->x2 - area->x1 + 1) * (size_t)(area->y2 - area->y1 + 1) * 2U;
    ESP_LOGE(TAG, "flush area %dx%d (%u B), largest DMA block %u B", (int)(area->x2 - area->x1 + 1),
             (int)(area->y2 - area->y1 + 1), (unsigned)strip_bytes, (unsigned)largest);
    /* Unblock LVGL — a corrupt strip beats an infinite WDT hang (E10/E30). */
    lv_display_flush_ready(drv);
}

/**
 * @brief Init QSPI panel (mirrors BSP, with deeper SPI transaction queue).
 * @param config Panel config (max_transfer_sz).
 * @param ret_panel Out panel handle.
 * @param ret_io Out panel IO handle.
 * @return ESP_OK on success.
 */
static esp_err_t watch_display_panel_new(const bsp_display_config_t *config,
                                         esp_lcd_panel_handle_t *ret_panel,
                                         esp_lcd_panel_io_handle_t *ret_io)
{
    if (config == NULL || config->max_transfer_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK, BSP_LCD_DATA0,
                                                                 BSP_LCD_DATA1, BSP_LCD_DATA2,
                                                                 BSP_LCD_DATA3, config->max_transfer_sz);
    esp_err_t err = spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = kSpiTransQueueDepth;

    esp_lcd_panel_io_handle_t io_handle = NULL;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io_handle);
    if (err != ESP_OK) {
        return err;
    }

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    err = esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_reset(panel_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_init(panel_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_set_gap(panel_handle, 0x16, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_disp_on_off(panel_handle, true);
    if (err != ESP_OK) {
        return err;
    }

    if (ret_panel != NULL) {
        *ret_panel = panel_handle;
    }
    if (ret_io != NULL) {
        *ret_io = io_handle;
    }
    return ESP_OK;
}

/**
 * @brief Register the panel with LVGL (QSPI / IO path, not RGB).
 * @return LVGL display handle, or NULL on failure.
 */
static lv_display_t *watch_display_lcd_init(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;

    const bsp_display_config_t panel_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT * BSP_LCD_BITS_PER_PIXEL / 8,
    };

    if (watch_display_panel_new(&panel_cfg, &panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return NULL;
    }

    s_io_handle = io;

    const uint32_t buffer_size = (uint32_t)BSP_LCD_H_RES * (uint32_t)CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT;

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = buffer_size,
        .double_buffer = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .sw_rotate = true,
            /* PSRAM draw buffer — keeps ~5.7 KB internal free for Wi-Fi init after NimBLE boot
             * init (~34 KB). Internal DMA buffer (buff_dma=true) broke esp_wifi_init with
             * esf_buf_setup_static alloc eb fail. Flush still uses a short-lived staging copy. */
            .buff_dma = false,
            .buff_spiram = true,
            .swap_bytes = true,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return NULL;
    }

    lv_display_set_flush_cb(disp, watch_flush_cb);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    ESP_LOGI(TAG, "LVGL display ready (QSPI IO path, queue=%d, %lu-line PSRAM buffer)",
             kSpiTransQueueDepth, (unsigned long)CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT);
    return disp;
}

/**
 * @brief Register touch input for the display.
 * @param disp LVGL display handle.
 * @return LVGL input device, or NULL on failure.
 */
static lv_indev_t *watch_display_indev_init(lv_display_t *disp)
{
    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(NULL, &touch) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed");
        return NULL;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch,
    };

    return lvgl_port_add_touch(&touch_cfg);
}

lv_display_t *watch_display_start(void)
{
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 1;

    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl port init failed");
        return NULL;
    }

    lv_display_t *disp = watch_display_lcd_init();
    if (disp == NULL) {
        return NULL;
    }

    s_touch_indev = watch_display_indev_init(disp);
    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "touch input init failed");
        return NULL;
    }

    if (watch_display_set_brightness(100) != ESP_OK) {
        ESP_LOGE(TAG, "brightness init failed");
        return NULL;
    }

    return disp;
}

bool watch_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void watch_display_unlock(void)
{
    lvgl_port_unlock();
}

lv_indev_t *watch_display_get_touch_indev(void)
{
    return s_touch_indev;
}
