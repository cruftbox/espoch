/*
 * WiFi connection + NTP time sync (Stage 3).
 *
 * net_time_start() is non-blocking: it kicks off the WiFi connection and, once
 * an IP is obtained, starts SNTP. The system clock is updated automatically in
 * the background, so the watch face picks up the real time on its next tick.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void net_time_start(void);

/* True once the watch has a WiFi connection (an IP address). */
bool net_time_is_connected(void);

#ifdef __cplusplus
}
#endif
