/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_NETWORK_H_
#define APP_UI_NETWORK_H_

#include <lvgl.h>

void ui_network_screen_create(void);
lv_obj_t *ui_network_screen_get(void);

/* Pauses Wi-Fi reconnect (scanning while STA-connected can hang the
 * driver), primes the status label, and pushes the screen. The single
 * entry point callers should use instead of a bare nav_push_screen().
 */
void ui_network_screen_enter(void);

/* Call frequently: pulls new Wi-Fi scan results into the screen, if a
 * scan is in progress.
 */
void ui_network_poll_scan(void);

#endif /* APP_UI_NETWORK_H_ */
