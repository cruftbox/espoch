/*
 * Step counter (Stage 4).
 *
 * A simple software pedometer: it streams accelerometer samples from the
 * QMI8658 IMU and counts steps with peak detection on the acceleration
 * magnitude. The count is for the current day and is reset at midnight by the
 * watch face (see main.c).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the IMU and start the background step-counting task. */
void step_counter_start(void);

/* Steps counted so far today. */
int step_counter_get(void);

/* Reset the count to zero (called at midnight). */
void step_counter_reset(void);

#ifdef __cplusplus
}
#endif
