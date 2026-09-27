/*
 * SPDX-License-Identifier: Apache-2.0
 */

/* gmtime_r needs a POSIX feature-test macro on native_sim's host glibc
 * (built with -std=c17); Zephyr's own libc doesn't need this but tolerates
 * it.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>

#include "ui_clock.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "ui_nav.h"

static lv_obj_t *scr_home;
static lv_obj_t *wifi_status_label;
static lv_obj_t *weekday_label;
static lv_obj_t *time_label;
static lv_obj_t *date_label;
static lv_obj_t *weather_label;

lv_obj_t *ui_home_screen_get(void)
{
    return scr_home;
}

static void home_settings_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    nav_push_screen(ui_menu_screen_get());
}

void ui_home_screen_create(void)
{
    scr_home = lv_obj_create(NULL);

    lv_obj_t *settings_btn = lv_button_create(scr_home);

    lv_obj_set_size(settings_btn, UI_ICON_BUTTON_SIZE, UI_ICON_BUTTON_SIZE);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -UI_EDGE_MARGIN, UI_EDGE_MARGIN);
    lv_obj_add_event_cb(settings_btn, home_settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_label = lv_label_create(settings_btn);

    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(settings_label);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);

    wifi_status_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_LEFT, UI_EDGE_MARGIN, UI_EDGE_MARGIN);
    lv_label_set_text(wifi_status_label, "Connecting...");

    time_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(time_label, "--:--:--");

    weekday_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(weekday_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(weekday_label, LV_ALIGN_CENTER, 0, 50);
    lv_label_set_text(weekday_label, "---");

    date_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 84);
    lv_label_set_text(date_label, "--- --");

    /* Placeholder until the weather.c client lands (Phase 2 of the
     * dashboard plan) - the layout slot exists now so Phase 2 only needs
     * to change this label's text, not add a new one.
     */
    weather_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(weather_label, LV_ALIGN_BOTTOM_MID, 0, -UI_EDGE_MARGIN);
    lv_label_set_text(weather_label, "Weather: --");
}

void ui_home_tick(int64_t epoch_s, enum wifi_net_status status)
{
    static const char *const weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    switch (status) {
    case WIFI_STATUS_CONNECTING:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WARNING " Connecting...");
        break;
    case WIFI_STATUS_CONNECTED:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_REFRESH " Syncing time...");
        break;
    case WIFI_STATUS_TIME_SYNCED:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI " Connected");
        break;
    }

    if (epoch_s == 0) {
        return;
    }

    time_t local_time = (time_t)(epoch_s + ui_clock_get_offset_seconds());
    struct tm tm_time;
    char buf[32];

    gmtime_r(&local_time, &tm_time);

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm_time.tm_hour, tm_time.tm_min,
             tm_time.tm_sec);
    lv_label_set_text(time_label, buf);

    lv_label_set_text(weekday_label, weekdays[tm_time.tm_wday]);

    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    snprintf(buf, sizeof(buf), "%s %d, %d", months[tm_time.tm_mon], tm_time.tm_mday,
             tm_time.tm_year + 1900);
    lv_label_set_text(date_label, buf);
}
