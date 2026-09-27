/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_clock.h"
#include "ui_common.h"
#include "ui_local.h"
#include "ui_nav.h"
#include "ui_network.h"
#include "ui_settings.h"
#include "ui_touch_cal.h"

static lv_obj_t *scr_settings;

lv_obj_t *ui_settings_screen_get(void)
{
    return scr_settings;
}

static void settings_network_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_network_screen_enter();
}

static void settings_clock_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    nav_push_screen(ui_clock_screen_get());
}

static void settings_touch_cal_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    nav_push_screen(ui_touch_cal_screen_get());
}

static void settings_local_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_local_screen_enter();
}

void ui_settings_screen_create(void)
{
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_settings, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_settings, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr_settings, 32, LV_PART_MAIN);

    lv_obj_t *network_btn = ui_add_menu_button(scr_settings, "Network", settings_network_click_cb);

    lv_obj_set_width(network_btn, 300);

    lv_obj_t *clock_btn = ui_add_menu_button(scr_settings, "Clock", settings_clock_click_cb);

    lv_obj_set_width(clock_btn, 300);

    lv_obj_t *touch_cal_btn = ui_add_menu_button(scr_settings, "Calibrate Touch",
                                                 settings_touch_cal_click_cb);

    lv_obj_set_width(touch_cal_btn, 300);

    lv_obj_t *local_btn = ui_add_menu_button(scr_settings, "Local", settings_local_click_cb);

    lv_obj_set_width(local_btn, 300);

    lv_obj_t *back_btn = ui_add_menu_button(scr_settings, "Back", nav_pop_screen);

    lv_obj_set_width(back_btn, 300);
}
