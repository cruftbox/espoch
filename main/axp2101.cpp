/*
 * AXP2101 port — C++ glue around Waveshare's XPowersLib, exposed to C.
 * See axp2101.h. Read-only: enables measurement ADCs, never touches rails.
 */
#include "axp2101.h"

#include <cstring>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"   /* bsp_i2c_get_handle() */

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "axp2101";

static XPowersPMU PMU;
static i2c_master_dev_handle_t s_dev = nullptr;
static bool s_ready = false;

/* Register read/write callbacks XPowersLib calls, bound to our I2C device. */
static int pmu_read(uint8_t dev_addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 1000) == ESP_OK ? 0 : -1;
}

static int pmu_write(uint8_t dev_addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    uint8_t buf[16];
    if ((size_t)(len + 1) > sizeof(buf)) {
        return -1;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, 1000) == ESP_OK ? 0 : -1;
}

extern "C" bool axp2101_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == nullptr) {
        ESP_LOGE(TAG, "I2C bus not available");
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = AXP2101_SLAVE_ADDRESS;   /* 0x34 */
    dev_cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "failed to add AXP2101 to I2C bus");
        return false;
    }

    if (!PMU.begin(AXP2101_SLAVE_ADDRESS, pmu_read, pmu_write)) {
        ESP_LOGE(TAG, "AXP2101 begin failed");
        return false;
    }

    /* Read-only configuration: enable the measurement paths only. */
    PMU.enableBattVoltageMeasure();
    PMU.enableVbusVoltageMeasure();
    PMU.enableSystemVoltageMeasure();
    /* This board has no battery temperature thermistor on the TS pin; leaving
     * TS detection enabled makes the AXP2101 refuse to charge. */
    PMU.disableTSPinMeasure();

    s_ready = true;
    ESP_LOGI(TAG, "AXP2101 ready — battery %d%%, vbus=%d, charging=%d",
             PMU.getBatteryPercent(), PMU.isVbusIn(), PMU.isCharging());
    return true;
}

extern "C" int axp2101_battery_percent(void)
{
    if (!s_ready || !PMU.isBatteryConnect()) {
        return -1;
    }
    return PMU.getBatteryPercent();
}

extern "C" bool axp2101_is_charging(void)
{
    return s_ready && PMU.isCharging();
}

extern "C" bool axp2101_vbus_present(void)
{
    return s_ready && PMU.isVbusIn();
}
