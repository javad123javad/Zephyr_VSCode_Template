/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"
#include "ui/ui_clock.h"
#include "ui/ui_home.h"
#include "ui/ui_menu.h"
#include "ui/ui_nav.h"
#include "ui/ui_network.h"
#include "ui/ui_password.h"
#include "ui/ui_placeholder.h"
#include "ui/ui_settings.h"
#include "ui/ui_touch_cal.h"

void ui_init(void)
{
    ui_clock_load_offset();

    /* Order doesn't matter for correctness (each screen is a standalone
     * lv_obj_t built once), but children-before-parents reads naturally
     * with how the menu hierarchy nests.
     */
    ui_home_screen_create();
    ui_menu_screen_create();
    ui_settings_screen_create();
    ui_network_screen_create();
    ui_clock_screen_create();
    ui_touch_cal_screen_create();
    ui_password_screen_create();
    ui_placeholder_screen_create();

    nav_load_screen(ui_home_screen_get());
}

void ui_tick(int64_t epoch_s, enum wifi_net_status status)
{
    ui_home_tick(epoch_s, status);
}

void ui_poll_wifi_scan(void)
{
    ui_network_poll_scan();
}
