/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_COMMON_H_
#define APP_UI_COMMON_H_

#include <lvgl.h>

/* Panel resolution (ST7796S, 480x320 landscape). Named instead of repeating
 * 480/320 across every screen file's layout code.
 */
#define UI_SCREEN_WIDTH  480
#define UI_SCREEN_HEIGHT 320

/* Common margin used for anything aligned to a screen edge. */
#define UI_EDGE_MARGIN 16

/* Standard interactive control heights used across screens. */
#define UI_BUTTON_HEIGHT      50
#define UI_MENU_BUTTON_HEIGHT 56
#define UI_ICON_BUTTON_SIZE   64

/* Creates a full-width text button inside parent, wired to cb on click.
 * Shared by every list-style menu screen (Main Menu, Settings, ...).
 */
lv_obj_t *ui_add_menu_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb);

#endif /* APP_UI_COMMON_H_ */
