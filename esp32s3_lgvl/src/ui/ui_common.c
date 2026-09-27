/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_common.h"

lv_obj_t *ui_add_menu_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_set_size(btn, LV_PCT(100), UI_MENU_BUTTON_HEIGHT);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);
    lv_label_set_text(label, text);

    return btn;
}
