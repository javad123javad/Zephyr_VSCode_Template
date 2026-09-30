/*
 * Fake stand-in for weather.c on native_sim: no real network/TLS stack
 * is exercised there, so this fabricates a fixed reading once a city has
 * been "set", purely so the LVGL UI can be exercised without hardware or
 * internet access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "weather.h"

#define SIM_TEMPERATURE_C 21.5

/* Cycles through one representative WMO code per scene so every Home
 * screen illustration can be seen on native_sim without a real forecast.
 */
#define SIM_SCENE_PERIOD_MS 8000

struct sim_condition {
    int32_t weather_code;
    const char *text;
};

static const struct sim_condition sim_conditions[] = {
    {.weather_code = 0, .text = "Clear"},
    {.weather_code = 2, .text = "Partly cloudy"},
    {.weather_code = 45, .text = "Fog"},
    {.weather_code = 61, .text = "Rain"},
    {.weather_code = 73, .text = "Snow"},
    {.weather_code = 80, .text = "Showers"},
    {.weather_code = 96, .text = "Thunderstorm"},
};

static enum weather_status sim_status = WEATHER_STATUS_NO_CITY;
static char sim_city[WEATHER_CITY_NAME_LEN];

static const struct sim_condition *sim_current_condition(void)
{
    size_t index = (k_uptime_get() / SIM_SCENE_PERIOD_MS) % ARRAY_SIZE(sim_conditions);

    return &sim_conditions[index];
}

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
    return sim_current_condition()->text;
}

enum weather_scene weather_get_scene(void)
{
    return weather_scene_from_code(sim_current_condition()->weather_code);
}

const char *weather_get_city_name(void)
{
    return sim_city;
}
