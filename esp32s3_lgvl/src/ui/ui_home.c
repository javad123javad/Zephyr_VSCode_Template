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
#include "ui_local.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_palette.h"
#include "weather.h"
#include "weather_scenes.h"

static lv_obj_t *scr_home;
static lv_obj_t *scene_img;
static bool scene_shown;
static enum weather_scene displayed_scene;

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

static void home_weather_cta_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_local_screen_enter();
}

/* --- Illustrated sky scenes ------------------------------------------- */

/* Each weather_scene's background is a single pre-rendered bitmap (see
 * weather_scenes.h and tools/gen_weather_scenes.py) instead of shapes
 * built from live LVGL objects: one lv_image object regardless of how
 * many circles/lines the art has, swapped by lv_image_set_src() when the
 * condition changes rather than rebuilt. The original hand-drawn version
 * cost a dozen-plus lv_obj_t per scene and twice caused this app's 64 KB
 * LV_Z_MEM_POOL_SIZE to run out (see AD-7 in docs/ARCHITECTURE.md); this
 * spends flash - barely used on this board - instead of the LVGL heap.
 */
static void show_scene(enum weather_scene scene)
{
    if (scene_shown && scene == displayed_scene) {
        return;
    }

    lv_image_set_src(scene_img, weather_scene_image(scene));
    displayed_scene = scene;
    scene_shown = true;
}

void ui_home_screen_create(void)
{
    scr_home = lv_obj_create(NULL);

    scene_img = lv_image_create(scr_home);
    lv_obj_remove_flag(scene_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(scene_img, 0, 0);

    /* A plain top-anchored label sits with its glyph top at the margin,
     * while the settings button below centers its icon inside a
     * UI_ICON_BUTTON_SIZE box - putting the two icons at different
     * heights. Mirror that box here (same size/margin, just not
     * clickable) so both icons center on the same line.
     */
    lv_obj_t *wifi_box = lv_obj_create(scr_home);

    lv_obj_remove_flag(wifi_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(wifi_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(wifi_box, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_box, 0, LV_PART_MAIN);
    lv_obj_set_size(wifi_box, UI_ICON_BUTTON_SIZE, UI_ICON_BUTTON_SIZE);
    lv_obj_align(wifi_box, LV_ALIGN_TOP_LEFT, UI_EDGE_MARGIN, UI_EDGE_MARGIN);

    wifi_status_label = lv_label_create(wifi_box);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(wifi_status_label);
    lv_label_set_text(wifi_status_label, LV_SYMBOL_WARNING);

    lv_obj_t *settings_btn = lv_button_create(scr_home);

    lv_obj_set_size(settings_btn, UI_ICON_BUTTON_SIZE, UI_ICON_BUTTON_SIZE);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -UI_EDGE_MARGIN, UI_EDGE_MARGIN);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(settings_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(settings_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(settings_btn, home_settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_label = lv_label_create(settings_btn);

    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(settings_label);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);

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

    weather_label = lv_label_create(scr_home);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(weather_label, LV_ALIGN_BOTTOM_MID, 0, -UI_EDGE_MARGIN);
    lv_obj_add_event_cb(weather_label, home_weather_cta_click_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(weather_label, "Weather: --");

    show_scene(weather_get_scene());
}

static void update_weather_label(enum weather_scene scene)
{
    switch (weather_get_status()) {
    case WEATHER_STATUS_NO_CITY:
        lv_obj_add_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_accent_color(scene), LV_PART_MAIN);
        lv_label_set_text(weather_label, LV_SYMBOL_RIGHT " Tap to set your city");
        break;
    case WEATHER_STATUS_PENDING:
        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_scene_text_color(scene), LV_PART_MAIN);
        lv_label_set_text(weather_label, "Weather: --");
        break;
    case WEATHER_STATUS_ERROR:
        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_warning_color(scene), LV_PART_MAIN);
        lv_label_set_text(weather_label, "Weather: unavailable");
        break;
    case WEATHER_STATUS_OK: {
        double temp_c;

        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_scene_text_color(scene), LV_PART_MAIN);
        if (weather_get_temperature_c(&temp_c)) {
            lv_label_set_text_fmt(weather_label, "%s: %.1fC %s", weather_get_city_name(), temp_c,
                                  weather_get_condition_text());
        } else {
            lv_label_set_text(weather_label, "Weather: --");
        }
        break;
    }
    }
}

void ui_home_tick(int64_t epoch_s, enum wifi_net_status status)
{
    static const char *const weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };
    enum weather_scene scene = weather_get_scene();
    lv_color_t text_color = ui_scene_text_color(scene);

    show_scene(scene);

    switch (status) {
    case WIFI_STATUS_CONNECTING:
        lv_obj_set_style_text_color(wifi_status_label, text_color, LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WARNING);
        break;
    case WIFI_STATUS_CONNECTED:
        lv_obj_set_style_text_color(wifi_status_label, text_color, LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_REFRESH);
        break;
    case WIFI_STATUS_TIME_SYNCED:
        lv_obj_set_style_text_color(wifi_status_label, text_color, LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI);
        break;
    case WIFI_STATUS_DISCONNECTED:
        lv_obj_set_style_text_color(wifi_status_label, ui_warning_color(scene), LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_CLOSE);
        break;
    }

    lv_obj_set_style_text_color(time_label, text_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(weekday_label, text_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(date_label, text_color, LV_PART_MAIN);

    /* Independent of the clock sync below: a city can be set (and
     * weather fetched) before time has ever synced.
     */
    update_weather_label(scene);

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
