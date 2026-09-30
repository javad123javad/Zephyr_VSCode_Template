/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_WEATHER_SCENES_H_
#define APP_UI_WEATHER_SCENES_H_

#include <lvgl.h>

#include "weather_scene.h"

/* Pre-rendered background for a given weather_scene (see
 * tools/gen_weather_scenes.py, which produced the raw pixel data this is
 * built from). Never NULL - falls back to the "unknown" scene's image for
 * an out-of-range value.
 */
const lv_image_dsc_t *weather_scene_image(enum weather_scene scene);

#endif /* APP_UI_WEATHER_SCENES_H_ */
