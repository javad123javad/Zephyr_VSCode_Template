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

#include <zephyr/sys/util.h>

#include "ui_clock.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_local.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_palette.h"
#include "weather.h"

#define HOME_CARD_RADIUS 20
#define HOME_CARD_OPA    LV_OPA_50

static lv_obj_t *scr_home;
static lv_obj_t *scene_layer;
static bool scene_built;
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

static void scene_obj_defaults(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
}

/* A filled circle (or, for a wide/short size, a capsule): the one shape
 * every scene decoration below is built from.
 */
static lv_obj_t *add_blob(lv_obj_t *parent, int32_t w, int32_t h, int32_t x, int32_t y,
                          lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);

    scene_obj_defaults(obj);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);

    return obj;
}

/* The glow is two larger, more transparent flat circles behind the solid
 * disc - not lv_obj's shadow styling, which asks LVGL's software renderer
 * to blur a temporary buffer sized off the shadow's width/spread
 * (lv_draw_sw_box_shadow.c). At this size that blur buffer alone (~45 KB
 * for a 90px sun) exceeds this app's entire 64 KB LVGL memory pool
 * (CONFIG_LV_Z_MEM_POOL_SIZE), and the failed allocation isn't handled
 * safely - it crashed the instant a sun scene was actually drawn, on both
 * native_sim and real hardware.
 */
static void add_sun(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t d)
{
    lv_color_t gold = lv_color_hex(0xFFD54F);

    add_blob(parent, d * 2, d * 2, cx - d, cy - d, gold, LV_OPA_20);
    add_blob(parent, (d * 3) / 2, (d * 3) / 2, cx - d * 3 / 4, cy - d * 3 / 4, gold, LV_OPA_30);
    add_blob(parent, d, d, cx - d / 2, cy - d / 2, gold, LV_OPA_COVER);
}

/* A handful of overlapping circles plus a wide capsule "base" to blend
 * their undersides into one silhouette - a cloud, drawn without any image
 * asset. (x, y) is the cloud's top-left; w is its overall width.
 */
static void add_cloud(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, lv_color_t color,
                      lv_opa_t opa)
{
    int32_t h = w / 2;

    add_blob(parent, (h * 3) / 2, (h * 3) / 2, x + h / 2, y, color, opa);
    add_blob(parent, (h * 5) / 4, (h * 5) / 4, x + h * 3 / 2, y + h / 4, color, opa);
    add_blob(parent, h, h, x, y + h / 2, color, opa);
    add_blob(parent, h, h, x + w - h, y + h / 2, color, opa);
    add_blob(parent, w, h, x, y + h / 2, color, opa);
}

static void add_rain(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t color)
{
    add_blob(parent, 4, 22, x, y, color, LV_OPA_70);
}

static void add_snow_dot(lv_obj_t *parent, int32_t x, int32_t y, int32_t d)
{
    add_blob(parent, d, d, x, y, lv_color_hex(0xFFFFFF), LV_OPA_90);
}

static void add_lightning(lv_obj_t *parent, int32_t x, int32_t y)
{
    static const lv_point_precise_t bolt_points[] = {
        {0, 0}, {14, 0}, {4, 20}, {16, 20}, {2, 46},
    };
    lv_obj_t *line = lv_line_create(parent);

    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_line_set_points(line, bolt_points, ARRAY_SIZE(bolt_points));
    lv_obj_set_style_line_width(line, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(0xFFF176), LV_PART_MAIN);
    lv_obj_set_pos(line, x, y);
}

static void populate_scene(lv_obj_t *layer, enum weather_scene scene)
{
    lv_color_t cloud_light = lv_color_hex(0xFFFFFF);
    lv_color_t cloud_grey = lv_color_hex(0xCFD8DC);
    lv_color_t cloud_dark = lv_color_hex(0x78909C);
    lv_color_t cloud_storm = lv_color_hex(0x37474F);
    lv_color_t rain_color = lv_color_hex(0xB3E5FC);
    static const int32_t rain_x[] = {90, 140, 190, 240, 290, 340, 390};
    static const int32_t snow_x[] = {70, 110, 150, 190, 230, 270, 310, 350, 390};

    switch (scene) {
    case WEATHER_SCENE_CLEAR:
        add_sun(layer, 110, 70, 90);
        break;
    case WEATHER_SCENE_PARTLY_CLOUDY:
        add_sun(layer, 90, 60, 70);
        add_cloud(layer, 130, 60, 170, cloud_light, LV_OPA_90);
        add_cloud(layer, 30, 120, 120, cloud_light, LV_OPA_70);
        break;
    case WEATHER_SCENE_FOG:
        add_cloud(layer, -20, 50, 220, cloud_grey, LV_OPA_50);
        add_cloud(layer, 190, 90, 240, cloud_grey, LV_OPA_40);
        add_cloud(layer, 50, 150, 280, cloud_grey, LV_OPA_50);
        break;
    case WEATHER_SCENE_RAIN:
        add_cloud(layer, 50, 30, 240, cloud_dark, LV_OPA_90);
        add_cloud(layer, 250, 60, 190, cloud_grey, LV_OPA_80);
        for (size_t i = 0U; i < ARRAY_SIZE(rain_x); i++) {
            add_rain(layer, rain_x[i], 150 + (int32_t)(i % 3) * 12, rain_color);
        }
        break;
    case WEATHER_SCENE_SNOW:
        add_cloud(layer, 70, 30, 210, cloud_grey, LV_OPA_90);
        add_cloud(layer, 260, 50, 190, cloud_light, LV_OPA_80);
        for (size_t i = 0U; i < ARRAY_SIZE(snow_x); i++) {
            add_snow_dot(layer, snow_x[i], 150 + (int32_t)(i % 4) * 20, 6 + (int32_t)(i % 3) * 2);
        }
        break;
    case WEATHER_SCENE_SHOWERS:
        add_sun(layer, 390, 55, 60);
        add_cloud(layer, 70, 40, 230, cloud_grey, LV_OPA_90);
        for (size_t i = 0U; i < 5U; i++) {
            add_rain(layer, rain_x[i] - 10, 150 + (int32_t)(i % 2) * 14, rain_color);
        }
        break;
    case WEATHER_SCENE_THUNDERSTORM:
        add_cloud(layer, 50, 20, 250, cloud_storm, LV_OPA_COVER);
        add_cloud(layer, 250, 40, 210, lv_color_hex(0x455A64), LV_OPA_COVER);
        add_lightning(layer, 200, 110);
        add_lightning(layer, 300, 120);
        for (size_t i = 0U; i < 4U; i++) {
            add_rain(layer, rain_x[i] * 3 / 2 - 40, 160, rain_color);
        }
        break;
    case WEATHER_SCENE_UNKNOWN:
    default:
        break;
    }
}

static lv_obj_t *build_scene(lv_obj_t *parent, enum weather_scene scene)
{
    lv_obj_t *layer = lv_obj_create(parent);
    lv_color_t top;
    lv_color_t bottom;

    ui_scene_sky_colors(scene, &top, &bottom);

    scene_obj_defaults(layer);
    lv_obj_set_style_radius(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(layer, 0, LV_PART_MAIN);
    lv_obj_set_size(layer, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, top, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(layer, bottom, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(layer, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, LV_PART_MAIN);

    /* Newly created, so it lands on top of scr_home's other children
     * (the scrim cards, settings button, ...) in z-order; put it back
     * behind them.
     */
    lv_obj_move_to_index(layer, 0);

    populate_scene(layer, scene);

    return layer;
}

/* Only one scene's worth of decoration objects (roughly a dozen circles/
 * lines) ever exists at a time, rebuilt on the rare occasions
 * weather_get_scene() actually changes - not all 8 scenes pre-built and
 * hidden. Pre-building all 8 up front (~100 objects total) fit this app's
 * 64 KB LV_Z_MEM_POOL_SIZE on the real (32-bit) hardware target, but not
 * on native_sim/native/64: LVGL's per-object structs carry several
 * pointers, so the same object count is noticeably larger on a 64-bit
 * host build, and it overran the pool - the same nominal byte budget
 * holds fewer, larger objects there. Building one scene at a time keeps
 * the peak object count (and so this bug) from scaling with the number
 * of scenes at all.
 */
static void show_scene(enum weather_scene scene)
{
    if (scene_built && scene == displayed_scene) {
        return;
    }

    if (scene_built) {
        lv_obj_delete(scene_layer);
    }

    scene_layer = build_scene(scr_home, scene);
    displayed_scene = scene;
    scene_built = true;
}

/* --- Translucent scrim cards (keep text legible over any sky scene) --- */

static lv_obj_t *make_scrim_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);

    scene_obj_defaults(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, HOME_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, HOME_CARD_OPA, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_size(card, w, h);

    return card;
}

void ui_home_screen_create(void)
{
    scr_home = lv_obj_create(NULL);

    lv_obj_t *wifi_card = make_scrim_card(scr_home, 210, 40);

    lv_obj_align(wifi_card, LV_ALIGN_TOP_LEFT, UI_EDGE_MARGIN, UI_EDGE_MARGIN);
    lv_obj_remove_flag(wifi_card, LV_OBJ_FLAG_CLICKABLE);

    wifi_status_label = lv_label_create(wifi_card);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(wifi_status_label);
    lv_label_set_text(wifi_status_label, "Connecting...");

    lv_obj_t *settings_btn = lv_button_create(scr_home);

    lv_obj_set_size(settings_btn, UI_ICON_BUTTON_SIZE, UI_ICON_BUTTON_SIZE);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -UI_EDGE_MARGIN, UI_EDGE_MARGIN);
    lv_obj_add_event_cb(settings_btn, home_settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_label = lv_label_create(settings_btn);

    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(settings_label);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);

    lv_obj_t *clock_card = make_scrim_card(scr_home, 340, 160);

    lv_obj_align(clock_card, LV_ALIGN_CENTER, 0, 6);

    time_label = lv_label_create(clock_card);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -34);
    lv_label_set_text(time_label, "--:--:--");

    weekday_label = lv_label_create(clock_card);
    lv_obj_set_style_text_font(weekday_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(weekday_label, LV_ALIGN_CENTER, 0, 22);
    lv_label_set_text(weekday_label, "---");

    date_label = lv_label_create(clock_card);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 54);
    lv_label_set_text(date_label, "--- --");

    lv_obj_t *weather_card = make_scrim_card(scr_home, 440, 40);

    lv_obj_align(weather_card, LV_ALIGN_BOTTOM_MID, 0, -UI_EDGE_MARGIN);

    weather_label = lv_label_create(weather_card);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(weather_label);
    lv_obj_add_event_cb(weather_label, home_weather_cta_click_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(weather_label, "Weather: --");

    show_scene(weather_get_scene());
}

static void update_weather_label(void)
{
    switch (weather_get_status()) {
    case WEATHER_STATUS_NO_CITY:
        lv_obj_add_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_accent_color(), LV_PART_MAIN);
        lv_label_set_text(weather_label, LV_SYMBOL_RIGHT " Tap to set your city");
        break;
    case WEATHER_STATUS_PENDING:
        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(weather_label, "Weather: --");
        break;
    case WEATHER_STATUS_ERROR:
        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, ui_warning_color(), LV_PART_MAIN);
        lv_label_set_text(weather_label, "Weather: unavailable");
        break;
    case WEATHER_STATUS_OK: {
        double temp_c;

        lv_obj_remove_flag(weather_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(weather_label, lv_color_white(), LV_PART_MAIN);
        if (weather_get_temperature_c(&temp_c)) {
            lv_label_set_text_fmt(weather_label, "%s: %.1fC %s", weather_get_city_name(), temp_c,
                                  weather_get_condition_text());
        } else {
            lv_label_set_text(weather_label, "Weather: --");
        }
        break;
    }
    }

    show_scene(weather_get_scene());
}

void ui_home_tick(int64_t epoch_s, enum wifi_net_status status)
{
    static const char *const weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    switch (status) {
    case WIFI_STATUS_CONNECTING:
        lv_obj_set_style_text_color(wifi_status_label, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WARNING " Connecting...");
        break;
    case WIFI_STATUS_CONNECTED:
        lv_obj_set_style_text_color(wifi_status_label, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_REFRESH " Syncing time...");
        break;
    case WIFI_STATUS_TIME_SYNCED:
        lv_obj_set_style_text_color(wifi_status_label, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI " Connected");
        break;
    case WIFI_STATUS_DISCONNECTED:
        lv_obj_set_style_text_color(wifi_status_label, ui_warning_color(), LV_PART_MAIN);
        lv_label_set_text(wifi_status_label, LV_SYMBOL_CLOSE " Offline");
        break;
    }

    /* Independent of the clock sync below: a city can be set (and
     * weather fetched) before time has ever synced.
     */
    update_weather_label();

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
