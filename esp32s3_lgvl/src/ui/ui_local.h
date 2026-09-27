/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_LOCAL_H_
#define APP_UI_LOCAL_H_

#include <lvgl.h>

void ui_local_screen_create(void);
lv_obj_t *ui_local_screen_get(void);

/* Sets the textarea's placeholder to the current city from weather.c
 * and pushes the screen, mirroring ui_password_screen_enter()'s
 * pattern of showing the current value as the field's placeholder
 * rather than a separate label (there's no screen space to spare for
 * one alongside the on-screen keyboard).
 */
void ui_local_screen_enter(void);

#endif /* APP_UI_LOCAL_H_ */
