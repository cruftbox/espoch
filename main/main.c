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
#include "nvs_flash.h"
#include "esp_app_desc.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "axp2101.h"
#include "net_time.h"
#include "watch_display.h"
#include "rtc_manager.h"
#include "timezone_manager.h"
#include "ota.h"

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
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_ver;

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

/* Runs once per second inside the LVGL task (mutex already held).
 *
 * Each field is only re-rendered when its value actually changes. On this panel,
 * redrawing every label every second makes adjacent redraws interfere (ghosting
 * / one row clipping the next), so we touch a label only when needed — usually
 * just the time. */
static void tick_cb(lv_timer_t *timer)
{
    (void)timer;

    static char prev_time[16] = "";
    static char prev_day[16]  = "";
    static char prev_date[24] = "";
    static int  prev_steps    = -1;
    static int  prev_batt     = -1;
    static int  prev_charging = -1;
    static int  prev_wifi     = -1;

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    char buf[40];

    /* 12-hour time, no leading zero, with AM/PM (e.g. "1:11:18 PM"). */
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    snprintf(buf, sizeof(buf), "%d:%02d:%02d %s",
             hour12, t.tm_min, t.tm_sec, t.tm_hour < 12 ? "AM" : "PM");
    if (strcmp(buf, prev_time) != 0) {
        lv_label_set_text(s_lbl_time, buf);
        strcpy(prev_time, buf);
    }

    strftime(buf, sizeof(buf), "%A", &t);            /* e.g. "Monday" */
    if (strcmp(buf, prev_day) != 0) {
        lv_label_set_text(s_lbl_day, buf);
        strcpy(prev_day, buf);
    }

    strftime(buf, sizeof(buf), "%B %e, %Y", &t);     /* e.g. "June 15, 2026" */
    if (strcmp(buf, prev_date) != 0) {
        lv_label_set_text(s_lbl_date, buf);
        strcpy(prev_date, buf);
    }

    if (s_steps != prev_steps) {
        lv_label_set_text_fmt(s_lbl_steps, "STEPS   %d,%03d", s_steps / 1000, s_steps % 1000);
        prev_steps = s_steps;
    }

    /* Live battery + charging status from the AXP2101. */
    int pct = axp2101_battery_percent();
    if (pct >= 0) {
        s_battery = pct;
    }
    int charging = axp2101_is_charging() ? 1 : 0;
    if (s_battery != prev_batt || charging != prev_charging) {
        if (charging) {
            lv_label_set_text_fmt(s_lbl_batt, "BATT   %d%% " LV_SYMBOL_CHARGE, s_battery);
        } else {
            lv_label_set_text_fmt(s_lbl_batt, "BATT   %d%%", s_battery);
        }
        lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(batt_color(s_battery)), 0);
        prev_batt = s_battery;
        prev_charging = charging;
    }

    /* WiFi symbol shows only while connected. */
    int wifi = net_time_is_connected() ? 1 : 0;
    if (wifi != prev_wifi) {
        lv_label_set_text(s_lbl_wifi, wifi ? LV_SYMBOL_WIFI : "");
        prev_wifi = wifi;
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            uint32_t color, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

static void build_watch_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 410 x 502 portrait. Stacked top-to-bottom per the dashboard mock, with a
     * WiFi status symbol at the very top. Generous vertical gaps so no row can
     * touch the next. */
    s_lbl_wifi  = make_label(scr, &lv_font_montserrat_28, COLOR_TIME,   18);
    s_lbl_day   = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,    70);  /* day + date moved */
    s_lbl_date  = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,   112);  /* up to the top      */
    s_lbl_time  = make_label(scr, &lv_font_montserrat_48, COLOR_TIME,  215);  /* time is the center */
    s_lbl_steps = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,   335);
    s_lbl_batt  = make_label(scr, &lv_font_montserrat_28, COLOR_DIM,   395);
    s_lbl_ver   = make_label(scr, &lv_font_montserrat_20, COLOR_DIM,   460);
    lv_label_set_text(s_lbl_wifi, "");   /* WiFi symbol appears once connected */
    /* Firmware version (from version.txt) — handy, and shows OTA updates landing. */
    lv_label_set_text_fmt(s_lbl_ver, "v%s", esp_app_get_description()->version);

    tick_cb(NULL);                       /* paint immediately, don't wait 1s */
    lv_timer_create(tick_cb, 1000, NULL);
}

/* Bring up the clock: timezone + hardware RTC. The PCF85063 RTC keeps time
 * across reboots, so the watch shows the right time immediately on boot (once
 * NTP has run at least once), even before WiFi reconnects. NTP later writes the
 * accurate time back to the RTC (see net_time.c). */
static void clock_init(void)
{
    timezone_manager_init();
    /* No on-device timezone picker yet (that needs the app/menu framework), so
     * force US Pacific with daylight saving until then. Index 4 = "US Pacific". */
    if (timezone_manager_get_selected_index() != 4) {
        timezone_manager_set_index(4);
    }
    if (!timezone_manager_dst_enabled()) {
        timezone_manager_set_dst_enabled(true);   /* PDT in summer, PST in winter */
    }

    rtc_manager_init();
    rtc_manager_ensure_time_set();   /* write a sane default if the RTC was never set */
    rtc_manager_sync_to_system();    /* load RTC time into the ESP32 system clock */
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESPoch booting...");
    ESP_LOGI(TAG, "Stage 2 — Watch Face");

    /* NVS is needed by the timezone store (and WiFi). */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    clock_init();

    /* QSPI IO-path display (Ben Brown's watch-os) — proper flush with retries +
     * 2-pixel rounding, replacing the BSP's broken RGB flush path. */
    if (watch_display_start() == NULL) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }

    if (!axp2101_init()) {                /* battery / charging readout (read-only) */
        ESP_LOGW(TAG, "AXP2101 init failed — battery readout disabled");
    }

    watch_display_lock(0);               /* LVGL is not thread-safe; take mutex */
    build_watch_face();
    watch_display_unlock();

    ESP_LOGI(TAG, "Watch face up");

    ota_mark_valid();                     /* boot reached a healthy state — no rollback */
    net_time_start();                     /* WiFi + NTP; updates the clock async */
    ota_start();                          /* background: check GitHub for a newer release */
}
