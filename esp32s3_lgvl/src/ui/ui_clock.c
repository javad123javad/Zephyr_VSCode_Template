/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/settings/settings.h>

#include "ui_clock.h"
#include "ui_common.h"
#include "ui_nav.h"

/* Change to your local UTC offset; also adjustable live from the Time zone
 * menu, which persists the chosen value to flash. Units are quarter-hours
 * (15 min) so half-hour and 45-minute zones (e.g. +5:30, +5:45) are
 * representable, not just whole hours.
 */
#define DEFAULT_TZ_OFFSET_QUARTERS 0
#define TZ_OFFSET_QUARTERS_MIN     (-12 * 4)
#define TZ_OFFSET_QUARTERS_MAX     (14 * 4)
#define TZ_MINUTES_PER_QUARTER     15
#define TZ_QUARTERS_PER_HOUR       4
#define TZ_SECONDS_PER_MINUTE      60

static lv_obj_t *scr_clock;
static lv_obj_t *tz_offset_label;
static int8_t tz_offset_quarters = DEFAULT_TZ_OFFSET_QUARTERS;
static bool tz_offset_dirty;

lv_obj_t *ui_clock_screen_get(void)
{
    return scr_clock;
}

int32_t ui_clock_get_offset_seconds(void)
{
    return (int32_t)tz_offset_quarters * TZ_MINUTES_PER_QUARTER * TZ_SECONDS_PER_MINUTE;
}

static void update_tz_offset_label(void)
{
    int quarters = tz_offset_quarters;
    const char *sign = quarters < 0 ? "-" : "+";
    int abs_quarters = quarters < 0 ? -quarters : quarters;

    lv_label_set_text_fmt(tz_offset_label, "%s%d:%02d", sign,
                          abs_quarters / TZ_QUARTERS_PER_HOUR,
                          (abs_quarters % TZ_QUARTERS_PER_HOUR) * TZ_MINUTES_PER_QUARTER);
}

static void save_tz_offset(void)
{
    int ret = settings_save_one("tz/offset_quarters", &tz_offset_quarters,
                                sizeof(tz_offset_quarters));

    if (ret != 0) {
        printf("Failed to save timezone offset: %d\n", ret);
    }
}

static int tz_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    if (settings_name_steq(name, "offset_quarters", NULL) &&
        len == sizeof(tz_offset_quarters)) {
        read_cb(cb_arg, &tz_offset_quarters, sizeof(tz_offset_quarters));
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tz, "tz", NULL, tz_settings_set, NULL, NULL);

void ui_clock_load_offset(void)
{
    settings_subsys_init();
    settings_load_subtree("tz");
}

static void tz_minus_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (tz_offset_quarters > TZ_OFFSET_QUARTERS_MIN) {
        tz_offset_quarters--;
        tz_offset_dirty = true;
        update_tz_offset_label();
    }
}

static void tz_plus_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (tz_offset_quarters < TZ_OFFSET_QUARTERS_MAX) {
        tz_offset_quarters++;
        tz_offset_dirty = true;
        update_tz_offset_label();
    }
}

static void tz_back_click_cb(lv_event_t *e)
{
    /* Save on the way out rather than on every tap: holding +/- to
     * repeat-step can fire many times per second, and the offset only
     * needs to be persisted once the user is done choosing it.
     */
    if (tz_offset_dirty) {
        save_tz_offset();
        tz_offset_dirty = false;
    }

    nav_pop_screen(e);
}

void ui_clock_screen_create(void)
{
    scr_clock = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_clock, 24, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr_clock);

    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "UTC offset (H:MM)");

    lv_obj_t *row = lv_obj_create(scr_clock);

    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 80);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *minus_btn = lv_button_create(row);

    lv_obj_set_size(minus_btn, 70, 70);
    lv_obj_add_event_cb(minus_btn, tz_minus_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(minus_btn, tz_minus_click_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    lv_obj_t *minus_label = lv_label_create(minus_btn);

    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(minus_label);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);

    tz_offset_label = lv_label_create(row);
    lv_obj_set_style_text_font(tz_offset_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_size(tz_offset_label, 100, 60);
    lv_obj_set_style_text_align(tz_offset_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    update_tz_offset_label();

    lv_obj_t *plus_btn = lv_button_create(row);

    lv_obj_set_size(plus_btn, 70, 70);
    lv_obj_add_event_cb(plus_btn, tz_plus_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(plus_btn, tz_plus_click_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    lv_obj_t *plus_label = lv_label_create(plus_btn);

    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(plus_label);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);

    lv_obj_t *back_btn = ui_add_menu_button(scr_clock, "Back", tz_back_click_cb);

    lv_obj_set_width(back_btn, 300);
}
