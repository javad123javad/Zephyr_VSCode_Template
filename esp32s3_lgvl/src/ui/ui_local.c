/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "ui_common.h"
#include "ui_local.h"
#include "ui_nav.h"
#include "weather.h"

static lv_obj_t *scr_local;
static lv_obj_t *city_ta;
static lv_obj_t *city_kb;
static lv_obj_t *current_label;

lv_obj_t *ui_local_screen_get(void)
{
    return scr_local;
}

void ui_local_screen_enter(void)
{
    const char *city = weather_get_city_name();

    if (city[0] != '\0') {
        lv_label_set_text_fmt(current_label, "Current: %s", city);
    } else {
        lv_label_set_text(current_label, "Current: not set");
    }

    lv_textarea_set_text(city_ta, "");
    nav_push_screen(scr_local);
}

static void local_confirm(void)
{
    const char *name = lv_textarea_get_text(city_ta);
    size_t len = strlen(name);

    if (len > 0U) {
        weather_set_city(name, len);
    }
}

static void local_confirm_click_cb(lv_event_t *e)
{
    local_confirm();
    nav_pop_screen(e);
}

static void local_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        local_confirm();
        nav_pop_screen(e);
    } else if (code == LV_EVENT_CANCEL) {
        nav_pop_screen(e);
    }
}

void ui_local_screen_create(void)
{
    scr_local = lv_obj_create(NULL);
    lv_obj_remove_flag(scr_local, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr_local, 8, LV_PART_MAIN);

    current_label = lv_label_create(scr_local);
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_pos(current_label, 8, 4);
    lv_label_set_text(current_label, "Current: not set");

    city_ta = lv_textarea_create(scr_local);
    lv_textarea_set_one_line(city_ta, true);
    lv_textarea_set_placeholder_text(city_ta, "City name");
    lv_obj_set_style_text_font(city_ta, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_size(city_ta, 464, 50);
    lv_obj_set_pos(city_ta, 8, 32);

    ui_add_entry_confirm_cancel_row(scr_local, 86, local_confirm_click_cb, nav_pop_screen);

    city_kb = lv_keyboard_create(scr_local);
    lv_keyboard_set_textarea(city_kb, city_ta);
    lv_obj_set_size(city_kb, 480, 180);
    lv_obj_set_pos(city_kb, 0, 128);
    lv_obj_add_event_cb(city_kb, local_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(city_kb, local_kb_event_cb, LV_EVENT_CANCEL, NULL);
}
