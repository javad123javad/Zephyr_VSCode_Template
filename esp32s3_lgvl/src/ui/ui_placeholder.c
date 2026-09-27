/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_common.h"
#include "ui_nav.h"
#include "ui_placeholder.h"

static lv_obj_t *scr_placeholder;
static lv_obj_t *placeholder_title;

void ui_placeholder_screen_enter(const char *feature_name)
{
    lv_label_set_text_fmt(placeholder_title, "%s\ncoming soon", feature_name);
    nav_push_screen(scr_placeholder);
}

void ui_placeholder_screen_create(void)
{
    scr_placeholder = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_placeholder, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_placeholder, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_placeholder, 24, LV_PART_MAIN);

    placeholder_title = lv_label_create(scr_placeholder);
    lv_obj_set_style_text_font(placeholder_title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(placeholder_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(placeholder_title, "");

    lv_obj_t *back_btn = ui_add_menu_button(scr_placeholder, "Back", nav_pop_screen);

    lv_obj_set_width(back_btn, 300);
}
