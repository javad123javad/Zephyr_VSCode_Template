/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_HOME_H_
#define APP_UI_HOME_H_

#include <stdint.h>

#include <lvgl.h>

#include "wifi.h"

void ui_home_screen_create(void);
lv_obj_t *ui_home_screen_get(void);

/* Call once a second: refreshes the clock/date/Wi-Fi-status labels. */
void ui_home_tick(int64_t epoch_s, enum wifi_net_status status);

#endif /* APP_UI_HOME_H_ */
