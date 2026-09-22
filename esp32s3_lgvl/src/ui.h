/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_H_
#define APP_UI_H_

#include <stdint.h>

#include "wifi.h"

void ui_init(void);

/* Call once a second: refreshes the clock screen's time/date/status. */
void ui_tick(int64_t epoch_s, enum wifi_net_status status);

/* Call frequently: pulls new Wi-Fi scan results into the Wi-Fi screen, if
 * a scan is in progress.
 */
void ui_poll_wifi_scan(void);

#endif /* APP_UI_H_ */
