/*
 * ESPoch — Stage 2: Watch Face
 *
 * Draws the dashboard watch face on the AMOLED: large amber time, day and
 * date, plus step-count and battery fields. Amber-on-black to suit the AMOLED
 * (black pixels are off -> best contrast and battery life).
 *
 * All the display, touch and power-up work is handled by the official Waveshare
 * board support package (BSP); we just create LVGL widgets on top of it.
 *
 * What is real in this stage vs. wired up later:
 *   - Time / day / date : LIVE and ticking. The clock is seeded to a fixed
 *                         value at boot, so it is NOT the true time yet — that
 *                         arrives in Stage 3 (NTP sync + timezone config).
 *   - Battery %         : PLACEHOLDER value for now. The color logic is real;
 *                         it just needs the AXP2101 reading wired in (the BSP
 *                         does not expose battery, so that is its own step).
 *   - Step count        : PLACEHOLDER until Stage 4 (QMI8658 IMU).
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "axp2101.h"

static const char *TAG = "espoch";

/* --- Color palette (from PROJECT.md) --- */
#define COLOR_TIME      0xFFB300   /* bright amber */
#define COLOR_DIM       0xCC8800   /* dimmer amber for date / labels */
#define COLOR_BATT_HI   0x00FF00   /* > 50%  green  */
#define COLOR_BATT_MID  0xFFFF00   /* 20-50% yellow */
#define COLOR_BATT_LO   0xFF0000   /* < 20%  red    */

/* --- Widgets we update every second --- */
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_day;
static lv_obj_t *s_lbl_date;
static lv_obj_t *s_lbl_steps;
static lv_obj_t *s_lbl_batt;

/* Steps are still a placeholder until Stage 4 (QMI8658 IMU). */
static int s_steps   = 2847;   /* TODO Stage 4: read from QMI8658 IMU */

/* Battery is read live from the AXP2101; this caches the last good value in
 * case a read fails. */
static int s_battery = 0;

static uint32_t batt_color(int pct)
{
    if (pct > 50) return COLOR_BATT_HI;
    if (pct >= 20) return COLOR_BATT_MID;
    return COLOR_BATT_LO;
}

/* Runs once per second inside the LVGL task (mutex already held). */
static void tick_cb(lv_timer_t *timer)
{
    (void)timer;

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    char buf[40];

    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    lv_label_set_text(s_lbl_time, buf);

    strftime(buf, sizeof(buf), "%A", &t);            /* e.g. "Monday" */
    lv_label_set_text(s_lbl_day, buf);

    strftime(buf, sizeof(buf), "%B %e, %Y", &t);     /* e.g. "June 14, 2026" */
    lv_label_set_text(s_lbl_date, buf);

    lv_label_set_text_fmt(s_lbl_steps, "STEPS   %d,%03d", s_steps / 1000, s_steps % 1000);

    /* Live battery + charging status from the AXP2101. */
    int pct = axp2101_battery_percent();
    if (pct >= 0) {
        s_battery = pct;
    }
    if (axp2101_is_charging()) {
        lv_label_set_text_fmt(s_lbl_batt, "BATT   %d%%  CHG", s_battery);
    } else {
        lv_label_set_text_fmt(s_lbl_batt, "BATT   %d%%", s_battery);
    }
    lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(batt_color(s_battery)), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            uint32_t color, lv_align_t align, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_align(lbl, align, 0, y);
    return lbl;
}

static void build_watch_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 410 x 502 portrait. Stacked top-to-bottom per the dashboard mock. */
    s_lbl_time  = make_label(scr, &lv_font_montserrat_48, COLOR_TIME, LV_ALIGN_TOP_MID,  70);
    s_lbl_day   = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,  LV_ALIGN_TOP_MID, 160);
    s_lbl_date  = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,  LV_ALIGN_TOP_MID, 205);
    s_lbl_steps = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,  LV_ALIGN_TOP_MID, 310);
    s_lbl_batt  = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,  LV_ALIGN_TOP_MID, 370);

    tick_cb(NULL);                       /* paint immediately, don't wait 1s */
    lv_timer_create(tick_cb, 1000, NULL);
}

/* Seed the system clock so the watch shows a plausible time before NTP exists.
 * 1781433720 = 2026-06-14 10:42:00 UTC. Replaced by real NTP time in Stage 3. */
static void seed_clock(void)
{
    struct timeval tv = { .tv_sec = 1781433720, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    setenv("TZ", "UTC0", 1);             /* real timezone is configured in Stage 3 */
    tzset();
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESPoch booting...");
    ESP_LOGI(TAG, "Stage 2 — Watch Face");

    seed_clock();

    bsp_display_start();                  /* brings up panel, touch, LVGL task */

    if (!axp2101_init()) {                /* battery / charging readout (read-only) */
        ESP_LOGW(TAG, "AXP2101 init failed — battery readout disabled");
    }

    bsp_display_lock(0);                  /* LVGL is not thread-safe; take mutex */
    build_watch_face();
    bsp_display_unlock();

    ESP_LOGI(TAG, "Watch face up");
}
