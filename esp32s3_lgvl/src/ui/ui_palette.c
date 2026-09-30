/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>

#include "ui_palette.h"

/* Scenes whose sky and/or shapes are light enough that white text and the
 * light accent/warning variants below would lose contrast against them.
 */
bool ui_scene_is_light(enum weather_scene scene)
{
    switch (scene) {
    case WEATHER_SCENE_PARTLY_CLOUDY:
    case WEATHER_SCENE_FOG:
    case WEATHER_SCENE_SNOW:
        return true;
    default:
        return false;
    }
}

lv_color_t ui_scene_text_color(enum weather_scene scene)
{
    return ui_scene_is_light(scene) ? lv_color_hex(0x1B242C) : lv_color_white();
}

lv_color_t ui_accent_color(enum weather_scene scene)
{
    return ui_scene_is_light(scene) ? lv_color_hex(0xA6740A) : lv_color_hex(0xFFC24B);
}

lv_color_t ui_warning_color(enum weather_scene scene)
{
    return ui_scene_is_light(scene) ? lv_color_hex(0xA3251E) : lv_color_hex(0xE5484D);
}

struct sky_colors {
    uint32_t top;
    uint32_t bottom;
};

/* One gradient per weather_scene. Chosen so each scene reads as a
 * distinct sky at a glance (clear/rain/snow/storm shouldn't need the
 * weather text to tell apart), while staying dark enough overall for an
 * always-on display.
 */
static const struct sky_colors scene_sky_colors[] = {
    [WEATHER_SCENE_CLEAR] = {.top = 0x1E88E5, .bottom = 0x64B5F6},
    [WEATHER_SCENE_PARTLY_CLOUDY] = {.top = 0x4A6FA5, .bottom = 0x8FA8C7},
    [WEATHER_SCENE_FOG] = {.top = 0x8B95A1, .bottom = 0xC7CDD3},
    [WEATHER_SCENE_RAIN] = {.top = 0x37474F, .bottom = 0x546E7A},
    [WEATHER_SCENE_SNOW] = {.top = 0x6E7F8D, .bottom = 0xE3EEF5},
    [WEATHER_SCENE_SHOWERS] = {.top = 0x455A64, .bottom = 0x78909C},
    [WEATHER_SCENE_THUNDERSTORM] = {.top = 0x1A1A2E, .bottom = 0x38304A},
    [WEATHER_SCENE_UNKNOWN] = {.top = 0x121212, .bottom = 0x1E1E1E},
};

void ui_scene_sky_colors(enum weather_scene scene, lv_color_t *out_top, lv_color_t *out_bottom)
{
    const struct sky_colors *colors = &scene_sky_colors[WEATHER_SCENE_UNKNOWN];

    if ((size_t)scene < ARRAY_SIZE(scene_sky_colors)) {
        colors = &scene_sky_colors[scene];
    }

    *out_top = lv_color_hex(colors->top);
    *out_bottom = lv_color_hex(colors->bottom);
}
