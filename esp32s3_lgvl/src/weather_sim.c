/*
 * Fake stand-in for weather.c on native_sim: no real network/TLS stack
 * is exercised there, so this fabricates a fixed reading once a city has
 * been "set", purely so the LVGL UI can be exercised without hardware or
 * internet access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/sys/util.h>

#include "weather.h"

#define SIM_TEMPERATURE_C 21.5
#define SIM_CONDITION_TEXT "Partly cloudy"

static enum weather_status sim_status = WEATHER_STATUS_NO_CITY;
static char sim_city[WEATHER_CITY_NAME_LEN];

void weather_init(void)
{
}

void weather_set_city(const char *name, size_t len)
{
	size_t copy_len = MIN(len, sizeof(sim_city) - 1U);

	memcpy(sim_city, name, copy_len);
	sim_city[copy_len] = '\0';
	sim_status = WEATHER_STATUS_OK;
}

void weather_poll(void)
{
}

enum weather_status weather_get_status(void)
{
	return sim_status;
}

bool weather_get_temperature_c(double *out)
{
	*out = SIM_TEMPERATURE_C;
	return sim_status == WEATHER_STATUS_OK;
}

const char *weather_get_condition_text(void)
{
	return SIM_CONDITION_TEXT;
}

const char *weather_get_city_name(void)
{
	return sim_city;
}
