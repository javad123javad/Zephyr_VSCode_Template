/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_scene.h"

enum weather_scene weather_scene_from_code(int32_t code)
{
    if (code == 0) {
        return WEATHER_SCENE_CLEAR;
    } else if (code >= 1 && code <= 3) {
        return WEATHER_SCENE_PARTLY_CLOUDY;
    } else if (code == 45 || code == 48) {
        return WEATHER_SCENE_FOG;
    } else if (code >= 51 && code <= 67) {
        return WEATHER_SCENE_RAIN;
    } else if (code >= 71 && code <= 77) {
        return WEATHER_SCENE_SNOW;
    } else if (code >= 80 && code <= 82) {
        return WEATHER_SCENE_SHOWERS;
    } else if (code >= 95 && code <= 99) {
        return WEATHER_SCENE_THUNDERSTORM;
    }

    return WEATHER_SCENE_UNKNOWN;
}
