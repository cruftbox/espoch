/*
 * Minimal, read-only interface to the AXP2101 power-management chip.
 *
 * Talks to the AXP2101 over the BSP's shared I2C bus to report battery level
 * and charging status. It deliberately does NOT change any power rails — the
 * rails are already powering the CPU and display correctly at boot, so we only
 * enable the measurement ADCs and read.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the AXP2101 on the BSP I2C bus. Returns true on success.
 * Call after bsp_display_start() (which brings the I2C bus up). */
bool axp2101_init(void);

/* Battery charge level, 0-100. Returns -1 if unavailable or no battery. */
int axp2101_battery_percent(void);

/* True while the battery is charging. */
bool axp2101_is_charging(void);

/* True while USB (VBUS) power is present. */
bool axp2101_vbus_present(void);

#ifdef __cplusplus
}
#endif
