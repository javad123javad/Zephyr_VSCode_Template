/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_PALETTE_H_
#define APP_UI_PALETTE_H_

#include <stdbool.h>

#include <lvgl.h>

#include "weather_scene.h"

/* True if a scene's sky and/or its shapes (e.g. partly-cloudy's or snow's
 * light clouds) are light enough that white text and light accent colors
 * lose contrast against them, unlike the darker tones everywhere else
 * (rain, storm, night-dark "unknown").
 */
bool ui_scene_is_light(enum weather_scene scene);

/* Primary (non-semantic) text color to use over a given scene: labels
 * have no fixed background of their own now that the Home screen sits
 * directly on the illustrated scene, so the right color depends on which
 * scene is showing, not a single app-wide constant.
 */
lv_color_t ui_scene_text_color(enum weather_scene scene);

/* App-wide accent color for primary/interactive elements (a tappable
 * prompt, an active state, ...), picked for contrast against scene.
 * Established here and meant to replace the ad-hoc
 * lv_palette_main(LV_PALETTE_BLUE/GREEN/...) calls scattered across
 * individual screens (e.g. ui_network.c) as they're each revisited.
 */
lv_color_t ui_accent_color(enum weather_scene scene);

/* App-wide warning/alert color, for error and destructive-action states
 * (weather fetch failed, Wi-Fi lost, "Forget" buttons, ...), picked for
 * contrast against scene.
 */
lv_color_t ui_warning_color(enum weather_scene scene);

/* Top and bottom colors of the Home screen's vertical sky gradient for a
 * given weather scene.
 */
void ui_scene_sky_colors(enum weather_scene scene, lv_color_t *out_top, lv_color_t *out_bottom);

#endif /* APP_UI_PALETTE_H_ */
