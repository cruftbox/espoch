/**
 * @file qmi8658.c
 * @brief QMI8658 hardware Wake-on-Motion (INT1 → GPIO 21).
 *
 * Adapted from Ben Brown's watch-os (github.com/benbrown/watch-os), used with
 * permission. Provides the IMU driver (init, motion streaming, raw read);
 * ESPoch's step counter is built on top in step_counter.c.
 */

#include "qmi8658.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qmi8658";

/** @brief QMI8658 INT1 on the Waveshare 2.06" watch (schematic QMI_INT1). */
static const gpio_num_t k_int_gpio = GPIO_NUM_21;

static const uint8_t QMI8658_ADDR = 0x6BU;
static const uint8_t REG_WHO_AM_I = 0x00U;
static const uint8_t REG_CTRL1 = 0x02U;
static const uint8_t REG_CTRL2 = 0x03U;
static const uint8_t REG_CTRL3 = 0x04U;
static const uint8_t REG_CTRL7 = 0x08U;
static const uint8_t REG_AX_L = 0x35U;
static const uint8_t REG_CTRL9 = 0x0AU;
static const uint8_t REG_CAL1_L = 0x0BU;
static const uint8_t REG_CAL1_H = 0x0CU;
static const uint8_t REG_STATUSINT = 0x2DU;
static const uint8_t REG_STATUS1 = 0x2FU;
static const uint8_t REG_RESET = 0x60U;
static const uint8_t QMI8658_CHIP_ID = 0x05U;
static const uint8_t CTRL9_CMD_WOM = 0x08U;
static const uint8_t CTRL9_CMD_ACK = 0x00U;
static const uint8_t CTRL7_ACC_EN = 0x01U;
static const uint8_t CTRL7_GYR_EN = 0x02U;

/** @brief ±2 g → 16384 LSB/g. */
static const float k_accel_lsb_per_g = 16384.0f;

/** @brief ±2048 °/s → 16 LSB/(°/s). */
static const float k_gyro_lsb_per_dps = 16.0f;

/** @brief WoM threshold in milligrams (1 mg/LSB in CAL1_L). Higher = less sensitive. */
static const uint8_t k_wom_threshold_mg = 220U;

static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static bool s_wom_active;
/** @brief QMI8658 WoM engine is configured (sleep or prior session). */
static bool s_wom_engaged;
/** @brief App requested streaming (e.g. lightsaber); restore after WoM wake. */
static bool s_streaming_wanted;
static bool s_streaming_active;
static qmi8658_motion_cb_t s_motion_cb;
static TaskHandle_t s_wom_task;

/**
 * @brief Write one register byte to the IMU.
 * @param reg Register address.
 * @param value Value to write.
 * @return ESP_OK on success.
 */
static esp_err_t qmi_write_u8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 1000);
}

/**
 * @brief Read one register byte from the IMU.
 * @param reg Register address.
 * @param out Output byte.
 * @return ESP_OK on success.
 */
static esp_err_t qmi_read_u8(uint8_t reg, uint8_t *out)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, 1, 1000);
}

/**
 * @brief Read a contiguous register block.
 * @param reg Starting register.
 * @param out Output buffer.
 * @param len Number of bytes to read.
 * @return ESP_OK on success.
 */
static esp_err_t qmi_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 1000);
}

/**
 * @brief Poll STATUSINT until the CTRL9 command completes.
 * @param wait_cleared True to wait until CmdDone clears (ACK path).
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout.
 */
static esp_err_t qmi_wait_ctrl9_done(bool wait_cleared)
{
    uint8_t statusint = 0;
    for (int i = 0; i < 100; ++i) {
        esp_err_t err = qmi_read_u8(REG_STATUSINT, &statusint);
        if (err != ESP_OK) {
            return err;
        }

        const bool done = (statusint & 0x80U) != 0U;
        if ((!wait_cleared && done) || (wait_cleared && !done)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGW(TAG, "CTRL9 wait timeout (STATUSINT=0x%02X)", statusint);
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Run the datasheet WoM configuration sequence.
 * @return ESP_OK on success.
 */
static esp_err_t qmi_configure_wom(void)
{
    esp_err_t err = qmi_write_u8(REG_CTRL7, 0x00U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CAL1_L, k_wom_threshold_mg);
    if (err != ESP_OK) {
        return err;
    }

    /* INT1, initial low, 8-sample blanking (ignore settle transients after arm). */
    err = qmi_write_u8(REG_CAL1_H, 0x08U);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t ctrl1 = 0;
    err = qmi_read_u8(REG_CTRL1, &ctrl1);
    if (err != ESP_OK) {
        return err;
    }
    ctrl1 |= 0x48U; /* auto-increment + INT1 enable */
    err = qmi_write_u8(REG_CTRL1, ctrl1);
    if (err != ESP_OK) {
        return err;
    }

    /* ±2 g, 62.5 Hz accel for WoM engine. */
    err = qmi_write_u8(REG_CTRL2, 0x07U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CTRL9, CTRL9_CMD_WOM);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_wait_ctrl9_done(false);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status1 = 0;
    (void)qmi_read_u8(REG_STATUS1, &status1);

    err = qmi_write_u8(REG_CTRL9, CTRL9_CMD_ACK);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_wait_ctrl9_done(true);
    if (err != ESP_OK) {
        return err;
    }

    s_wom_engaged = true;
    return qmi_write_u8(REG_CTRL7, CTRL7_ACC_EN);
}

/**
 * @brief Exit Wake-on-Motion per QMI8658 datasheet §9.6 (required before gyro streaming).
 * @return ESP_OK on success.
 */
static esp_err_t qmi_exit_wom_mode(void)
{
    if (!s_wom_engaged) {
        return ESP_OK;
    }

    esp_err_t err = qmi_write_u8(REG_CTRL7, 0x00U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CAL1_L, 0x00U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CAL1_H, 0x04U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CTRL9, CTRL9_CMD_WOM);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_wait_ctrl9_done(false);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status1 = 0;
    (void)qmi_read_u8(REG_STATUS1, &status1);

    err = qmi_write_u8(REG_CTRL9, CTRL9_CMD_ACK);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_wait_ctrl9_done(true);
    if (err != ESP_OK) {
        return err;
    }

    s_wom_engaged = false;
    ESP_LOGI(TAG, "WoM engine exited");
    return ESP_OK;
}

/**
 * @brief GPIO ISR — notify the WoM task (no I2C in ISR).
 * @param arg Unused.
 */
static void IRAM_ATTR wom_gpio_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    if (s_wom_task != NULL) {
        xTaskNotifyFromISR(s_wom_task, 1, eSetBits, &hp);
    }
    portYIELD_FROM_ISR(hp);
}

/**
 * @brief Read STATUS1 and invoke the motion callback on a real WoM event.
 * @param arg Unused.
 */
static void wom_task(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t note = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &note, portMAX_DELAY);
        if (!s_wom_active || s_motion_cb == NULL) {
            continue;
        }

        uint8_t status1 = 0;
        if (qmi_read_u8(REG_STATUS1, &status1) != ESP_OK) {
            continue;
        }

        if ((status1 & 0x04U) != 0U) {
            ESP_LOGI(TAG, "WoM motion (STATUS1=0x%02X)", status1);
            s_motion_cb();
        }
    }
}

esp_err_t qmi8658_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(bsp_i2c_init());

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_ADDR,
        .scl_speed_hz = 400000,
    };

    esp_err_t err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: %s", esp_err_to_name(err));
        return err;
    }

    err = qmi_write_u8(REG_RESET, 0xB0U);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(15));

    uint8_t chip_id = 0;
    err = qmi_read_u8(REG_WHO_AM_I, &chip_id);
    if (err != ESP_OK || chip_id != QMI8658_CHIP_ID) {
        ESP_LOGE(TAG, "WHO_AM_I 0x%02X (expected 0x%02X)", chip_id, QMI8658_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    err = qmi_write_u8(REG_CTRL1, 0x40U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CTRL7, 0x00U);
    if (err != ESP_OK) {
        return err;
    }

    if (s_wom_task == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(wom_task, "qmi_wom", 3072, NULL, 5, &s_wom_task, 0);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 ready (hardware WoM)");
    return ESP_OK;
}

bool qmi8658_is_ready(void)
{
    return s_ready;
}

/**
 * @brief Apply 250 Hz accel+gyro streaming register set.
 * @return ESP_OK on success.
 */
static esp_err_t qmi_apply_streaming_config(void)
{
    esp_err_t err = qmi_exit_wom_mode();
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CTRL1, 0x40U);
    if (err != ESP_OK) {
        return err;
    }

    /* ±2 g, 250 Hz. */
    err = qmi_write_u8(REG_CTRL2, 0x05U);
    if (err != ESP_OK) {
        return err;
    }

    /* ±2048 °/s, 250 Hz. */
    err = qmi_write_u8(REG_CTRL3, 0x75U);
    if (err != ESP_OK) {
        return err;
    }

    err = qmi_write_u8(REG_CTRL7, CTRL7_ACC_EN | CTRL7_GYR_EN);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t qmi8658_enable_wake_on_motion(qmi8658_motion_cb_t on_motion)
{
    if (!s_ready || on_motion == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_motion_cb = on_motion;
    s_streaming_active = false;

    esp_err_t err = qmi_configure_wom();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WoM configure failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_wom_active) {
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }

        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << k_int_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return err;
        }

        err = gpio_isr_handler_add(k_int_gpio, wom_gpio_isr, NULL);
        if (err != ESP_OK) {
            return err;
        }

        s_wom_active = true;
    }

    ESP_LOGI(TAG, "WoM armed (%u mg threshold, INT1 GPIO%d)", (unsigned)k_wom_threshold_mg,
             (int)k_int_gpio);
    return ESP_OK;
}

esp_err_t qmi8658_disable_wake_on_motion(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wom_active) {
        gpio_isr_handler_remove(k_int_gpio);
        s_wom_active = false;
    }

    s_motion_cb = NULL;

    if (s_streaming_wanted) {
        esp_err_t err = qmi_apply_streaming_config();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "streaming restore after WoM failed: %s", esp_err_to_name(err));
            return err;
        }
        s_streaming_active = true;
        ESP_LOGI(TAG, "6-axis streaming restored after WoM");
        return ESP_OK;
    }

    return qmi_exit_wom_mode();
}

esp_err_t qmi8658_enable_streaming(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wom_active) {
        gpio_isr_handler_remove(k_int_gpio);
        s_wom_active = false;
        s_motion_cb = NULL;
    }

    esp_err_t err = qmi_apply_streaming_config();
    if (err != ESP_OK) {
        return err;
    }

    s_streaming_wanted = true;
    s_streaming_active = true;
    ESP_LOGI(TAG, "6-axis streaming on");
    return ESP_OK;
}

esp_err_t qmi8658_disable_streaming(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    s_streaming_wanted = false;
    s_streaming_active = false;
    return qmi_write_u8(REG_CTRL7, 0x00U);
}

bool qmi8658_streaming_active(void)
{
    return s_streaming_active;
}

esp_err_t qmi8658_read_motion(qmi8658_motion_sample_t *out)
{
    if (!s_ready || !s_streaming_active || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6];
    esp_err_t err = qmi_read(REG_AX_L, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    const int16_t ax = (int16_t)((data[1] << 8) | data[0]);
    const int16_t ay = (int16_t)((data[3] << 8) | data[2]);
    const int16_t az = (int16_t)((data[5] << 8) | data[4]);

    err = qmi_read(0x3BU, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    const int16_t gx = (int16_t)((data[1] << 8) | data[0]);
    const int16_t gy = (int16_t)((data[3] << 8) | data[2]);
    const int16_t gz = (int16_t)((data[5] << 8) | data[4]);

    out->accel_g[0] = (float)ax / k_accel_lsb_per_g;
    out->accel_g[1] = (float)ay / k_accel_lsb_per_g;
    out->accel_g[2] = (float)az / k_accel_lsb_per_g;
    out->gyro_dps[0] = (float)gx / k_gyro_lsb_per_dps;
    out->gyro_dps[1] = (float)gy / k_gyro_lsb_per_dps;
    out->gyro_dps[2] = (float)gz / k_gyro_lsb_per_dps;
    return ESP_OK;
}

/* ============================================================================
 * Hardware pedometer (QMI8658 built-in step engine) — added for ESPoch.
 * Register sequence per the QST QMI8658 datasheet and the reference
 * implementation in lewisxhe/SensorLib (SensorQMI8658).
 * ==========================================================================*/

static const uint8_t REG_CTRL8      = 0x09U;
static const uint8_t REG_CAL2_L     = 0x0DU;
static const uint8_t REG_CAL2_H     = 0x0EU;
static const uint8_t REG_CAL3_L     = 0x0FU;
static const uint8_t REG_CAL3_H     = 0x10U;
static const uint8_t REG_CAL4_L     = 0x11U;
static const uint8_t REG_CAL4_H     = 0x12U;
static const uint8_t REG_STEP_CNT_L = 0x5AU;
static const uint8_t CTRL9_CMD_CONFIG_PEDOMETER = 0x0DU;
static const uint8_t CTRL8_PEDO_EN  = 0x10U;   /* CTRL8 bit 4 */

/* Issue a CTRL9 command and complete the handshake (wait done -> ACK -> clear). */
static esp_err_t qmi_ctrl9_command(uint8_t cmd)
{
    esp_err_t err = qmi_write_u8(REG_CTRL9, cmd);
    if (err != ESP_OK) {
        return err;
    }
    qmi_wait_ctrl9_done(false);
    err = qmi_write_u8(REG_CTRL9, CTRL9_CMD_ACK);
    if (err != ESP_OK) {
        return err;
    }
    qmi_wait_ctrl9_done(true);
    return ESP_OK;
}

static esp_err_t qmi_write_u16(uint8_t reg_l, uint8_t reg_h, uint16_t v)
{
    esp_err_t err = qmi_write_u8(reg_l, (uint8_t)(v & 0xFFU));
    if (err != ESP_OK) {
        return err;
    }
    return qmi_write_u8(reg_h, (uint8_t)((v >> 8) & 0xFFU));
}

esp_err_t qmi8658_pedometer_enable(void)
{
    /* Strict "datasheet" profile — vendor-tuned to reject non-step vibration.
     * entry_count = 8 means 8 gait steps must register before counting starts,
     * which is what rejects hand movements. Timing params are in accel samples,
     * scaled for the 250 Hz streaming ODR (4 ms/sample): 2000 ms -> 500, 300 -> 75. */
    const uint16_t sample_count   = 50U;
    const uint16_t peak_to_peak   = 100U;  /* mg */
    const uint16_t peak_threshold = 116U;  /* mg */
    const uint16_t time_up        = 500U;
    const uint8_t  time_low       = 75U;
    const uint8_t  entry_count    = 8U;
    const uint8_t  fix_precision  = 0U;
    const uint8_t  sig_count      = 1U;

    /* Vendor flow configures the engine with the sensors briefly off. */
    uint8_t ctrl7 = 0U;
    qmi_read_u8(REG_CTRL7, &ctrl7);
    qmi_write_u8(REG_CTRL7, 0x00U);

    /* Phase 1 — peak parameters (CAL4 = 0x01/0x02 selects this sub-page). */
    esp_err_t err = qmi_write_u16(REG_CAL1_L, REG_CAL1_H, sample_count);
    if (err == ESP_OK) err = qmi_write_u16(REG_CAL2_L, REG_CAL2_H, peak_to_peak);
    if (err == ESP_OK) err = qmi_write_u16(REG_CAL3_L, REG_CAL3_H, peak_threshold);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL4_H, 0x01U);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL4_L, 0x02U);
    if (err == ESP_OK) err = qmi_ctrl9_command(CTRL9_CMD_CONFIG_PEDOMETER);

    /* Phase 2 — timing parameters (CAL4 = 0x02/0x02). */
    if (err == ESP_OK) err = qmi_write_u16(REG_CAL1_L, REG_CAL1_H, time_up);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL2_L, time_low);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL2_H, entry_count);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL3_L, fix_precision);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL3_H, sig_count);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL4_H, 0x02U);
    if (err == ESP_OK) err = qmi_write_u8(REG_CAL4_L, 0x02U);
    if (err == ESP_OK) err = qmi_ctrl9_command(CTRL9_CMD_CONFIG_PEDOMETER);

    /* Restore sensors, then toggle Pedo_EN 0->1 to start (and zero) the engine. */
    qmi_write_u8(REG_CTRL7, ctrl7);
    uint8_t ctrl8 = 0U;
    qmi_read_u8(REG_CTRL8, &ctrl8);
    qmi_write_u8(REG_CTRL8, (uint8_t)(ctrl8 & ~CTRL8_PEDO_EN));
    qmi_read_u8(REG_CTRL8, &ctrl8);
    err = qmi_write_u8(REG_CTRL8, (uint8_t)(ctrl8 | CTRL8_PEDO_EN));

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "hardware pedometer enabled (entry_count=%u)", entry_count);
    } else {
        ESP_LOGE(TAG, "pedometer enable failed: %s", esp_err_to_name(err));
    }
    return err;
}

uint32_t qmi8658_pedometer_count(void)
{
    uint8_t b[3] = {0};
    if (qmi_read(REG_STEP_CNT_L, b, sizeof(b)) != ESP_OK) {
        return 0U;
    }
    return ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}

esp_err_t qmi8658_pedometer_reset(void)
{
    /* Toggling Pedo_EN 0->1 resets the step count (per datasheet). */
    uint8_t ctrl8 = 0U;
    qmi_read_u8(REG_CTRL8, &ctrl8);
    qmi_write_u8(REG_CTRL8, (uint8_t)(ctrl8 & ~CTRL8_PEDO_EN));
    qmi_read_u8(REG_CTRL8, &ctrl8);
    return qmi_write_u8(REG_CTRL8, (uint8_t)(ctrl8 | CTRL8_PEDO_EN));
}
