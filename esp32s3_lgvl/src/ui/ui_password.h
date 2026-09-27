/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_PASSWORD_H_
#define APP_UI_PASSWORD_H_

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

void ui_password_screen_create(void);
lv_obj_t *ui_password_screen_get(void);

/* Sets which SSID a successful entry connects to, then pushes the screen
 * (Cancel pops back to whatever screen called this, same as any other
 * screen's Back button).
 */
void ui_password_screen_enter(const char *ssid, uint8_t ssid_len);

#endif /* APP_UI_PASSWORD_H_ */
