/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_nav.h"
#include "ui_touch_cal.h"

#define UI_TOUCH_CAL_DOT_SIZE 30

static lv_obj_t *scr_touch_cal;
static lv_obj_t *touch_cal_dot;
static lv_obj_t *touch_cal_label;

lv_obj_t *ui_touch_cal_screen_get(void)
{
    return scr_touch_cal;
}

static void touch_cal_press_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;

    lv_indev_get_point(indev, &p);
    lv_obj_set_pos(touch_cal_dot, p.x - UI_TOUCH_CAL_DOT_SIZE / 2,
                  p.y - UI_TOUCH_CAL_DOT_SIZE / 2);
    lv_label_set_text_fmt(touch_cal_label, "x=%d y=%d", (int)p.x, (int)p.y);
}

void ui_touch_cal_screen_create(void)
{
    scr_touch_cal = lv_obj_create(NULL);
    lv_obj_add_flag(scr_touch_cal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_touch_cal, touch_cal_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr_touch_cal, touch_cal_press_cb, LV_EVENT_PRESSED, NULL);

    touch_cal_label = lv_label_create(scr_touch_cal);
    lv_obj_set_style_text_font(touch_cal_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(touch_cal_label, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(touch_cal_label, "Tap anywhere - dot should follow your finger");

    touch_cal_dot = lv_obj_create(scr_touch_cal);
    lv_obj_remove_style_all(touch_cal_dot);
    lv_obj_set_size(touch_cal_dot, UI_TOUCH_CAL_DOT_SIZE, UI_TOUCH_CAL_DOT_SIZE);
    lv_obj_set_style_radius(touch_cal_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(touch_cal_dot, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(touch_cal_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(touch_cal_dot);

    lv_obj_t *done_btn = lv_button_create(scr_touch_cal);

    lv_obj_set_size(done_btn, 130, 50);
    lv_obj_align(done_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(done_btn, nav_pop_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_label = lv_label_create(done_btn);

    lv_obj_set_style_text_font(done_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(done_label);
    lv_label_set_text(done_label, "Done");
}
