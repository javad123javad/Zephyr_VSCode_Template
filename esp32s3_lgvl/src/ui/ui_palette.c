/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_palette.h"

/* Scenes whose sky and/or shapes are light enough that white text and the
 * light accent/warning variants below would lose contrast against them.
 * The actual pixel colors live in tools/gen_weather_scenes.py now (Home's
 * background is a pre-rendered bitmap, not drawn from these colors at
 * runtime); keep this classification in sync with that script's palette.
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
