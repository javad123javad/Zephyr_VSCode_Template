/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_NAV_H_
#define APP_UI_NAV_H_

#include <lvgl.h>

/* Loads screen and remembers the caller's screen so nav_pop_screen() can
 * return to it. Used for the "forward" half of every navigation button.
 */
void nav_push_screen(lv_obj_t *screen);

/* Loads the previous screen from the stack. Used as the event callback for
 * every "Back" button so screens don't each hardcode their own parent.
 */
void nav_pop_screen(lv_event_t *e);

/* Loads screen directly without touching the stack. Used only for the
 * initial boot screen and for jumps that intentionally reset navigation
 * (e.g. returning to Home after finishing a task like Wi-Fi password
 * entry).
 */
void nav_load_screen(lv_obj_t *screen);

#endif /* APP_UI_NAV_H_ */
