/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_PALETTE_H_
#define APP_UI_PALETTE_H_

#include <lvgl.h>

#include "weather_scene.h"

/* App-wide accent color for primary/interactive elements (a tappable
 * prompt, an active state, ...). Established here and meant to replace
 * the ad-hoc lv_palette_main(LV_PALETTE_BLUE/GREEN/...) calls scattered
 * across individual screens (e.g. ui_network.c) as they're each revisited.
 */
lv_color_t ui_accent_color(void);

/* App-wide warning/alert color, for error and destructive-action states
 * (weather fetch failed, Wi-Fi lost, "Forget" buttons, ...).
 */
lv_color_t ui_warning_color(void);

/* Top and bottom colors of the Home screen's vertical sky gradient for a
 * given weather scene.
 */
void ui_scene_sky_colors(enum weather_scene scene, lv_color_t *out_top, lv_color_t *out_bottom);

#endif /* APP_UI_PALETTE_H_ */
