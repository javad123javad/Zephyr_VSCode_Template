/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_WEATHER_H_
#define APP_WEATHER_H_

#include <stdbool.h>
#include <stddef.h>

#define WEATHER_CITY_NAME_LEN 32U

enum weather_status {
    WEATHER_STATUS_NO_CITY,
    WEATHER_STATUS_PENDING,
    WEATHER_STATUS_OK,
    WEATHER_STATUS_ERROR,
};

/* Loads any previously-saved city/location from flash; call once at
 * startup, before any screen needs to display weather.
 */
void weather_init(void);

/* Requests a new city by name (from the Local settings screen). Does not
 * block: the actual geocoding request happens from weather_poll(), on the
 * network thread.
 */
void weather_set_city(const char *name, size_t len);

/* Call periodically from the network thread (see wifi.c), only while
 * Wi-Fi is connected: resolves a pending city name, or refreshes the
 * cached forecast once it's stale.
 */
void weather_poll(void);

enum weather_status weather_get_status(void);

/* Last fetched temperature in Celsius. Returns false if none is available
 * yet (no city set, or the first fetch hasn't completed).
 */
bool weather_get_temperature_c(double *out);

/* Short human-readable condition text (e.g. "Clear", "Rain"), or "" if
 * none is available yet.
 */
const char *weather_get_condition_text(void);

/* Resolved city name (may differ slightly from what was typed, e.g. the
 * geocoding API's own capitalization), or "" if none is set.
 */
const char *weather_get_city_name(void);

#endif /* APP_WEATHER_H_ */
