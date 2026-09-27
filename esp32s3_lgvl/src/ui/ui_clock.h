/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_CLOCK_H_
#define APP_UI_CLOCK_H_

#include <stdint.h>

#include <lvgl.h>

void ui_clock_screen_create(void);
lv_obj_t *ui_clock_screen_get(void);

/* Loads the persisted UTC offset from flash; call once at startup, before
 * any screen needs to display local time.
 */
void ui_clock_load_offset(void);

/* Current UTC offset in seconds, applied to an epoch time to get local
 * time. Quarter-hour resolution (supports e.g. +5:30, +5:45), not just
 * whole hours.
 */
int32_t ui_clock_get_offset_seconds(void);

#endif /* APP_UI_CLOCK_H_ */
