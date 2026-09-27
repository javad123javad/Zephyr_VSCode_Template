/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_LOCAL_H_
#define APP_UI_LOCAL_H_

#include <lvgl.h>

void ui_local_screen_create(void);
lv_obj_t *ui_local_screen_get(void);

/* Refreshes the "Current: <city>" label from weather.c and pushes the
 * screen, mirroring ui_password_screen_enter()'s pattern of doing
 * screen-specific setup before nav_push_screen().
 */
void ui_local_screen_enter(void);

#endif /* APP_UI_LOCAL_H_ */
