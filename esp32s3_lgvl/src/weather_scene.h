/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_WEATHER_SCENE_H_
#define APP_WEATHER_SCENE_H_

#include <stdint.h>

/* Which illustrated Home-screen sky scene (see ui_home.c) a WMO weather
 * code maps to. Kept separate from weather.c, and its cached_weather_code/
 * sockets/TLS dependencies, so this pure mapping links standalone in
 * tests/weather - the same reason weather_json.c was split out (AD-5 in
 * docs/ARCHITECTURE.md).
 */
enum weather_scene {
    WEATHER_SCENE_CLEAR,
    WEATHER_SCENE_PARTLY_CLOUDY,
    WEATHER_SCENE_FOG,
    WEATHER_SCENE_RAIN,
    WEATHER_SCENE_SNOW,
    WEATHER_SCENE_SHOWERS,
    WEATHER_SCENE_THUNDERSTORM,
    WEATHER_SCENE_UNKNOWN,
};

/* Maps an Open-Meteo/WMO weather_code (see https://open-meteo.com/en/docs,
 * "WMO Weather interpretation codes") to a scene. Ranges match
 * weather_get_condition_text()'s grouping in weather.c.
 */
enum weather_scene weather_scene_from_code(int32_t code);

#endif /* APP_WEATHER_SCENE_H_ */
