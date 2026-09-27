/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_BACKLIGHT_H_
#define APP_BACKLIGHT_H_

/* Configures the PWM backlight and sets it to full brightness. */
void backlight_init(void);

/* Call frequently (the existing ~10 ms main loop tick is fine): dims the
 * backlight after a period of no touch input, restoring full brightness on
 * the next touch. Uses LVGL's own lv_display_get_inactive_time() rather
 * than tracking touch activity separately.
 */
void backlight_tick(void);

#endif /* APP_BACKLIGHT_H_ */
