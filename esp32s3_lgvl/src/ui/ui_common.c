/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_common.h"

#define UI_ENTRY_ROW_GAP 16

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

static lv_obj_t *add_entry_row_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                      const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_set_size(btn, width, UI_ENTRY_ROW_HEIGHT);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);
    lv_label_set_text(label, text);

    return btn;
}

void ui_add_entry_confirm_cancel_row(lv_obj_t *parent, int32_t y, lv_event_cb_t confirm_cb,
                                     lv_event_cb_t cancel_cb)
{
    int32_t button_width = (UI_SCREEN_WIDTH - (2 * UI_EDGE_MARGIN) - UI_ENTRY_ROW_GAP) / 2;

    add_entry_row_button(parent, UI_EDGE_MARGIN, y, button_width, LV_SYMBOL_CLOSE " Cancel",
                         cancel_cb);
    add_entry_row_button(parent, UI_EDGE_MARGIN + button_width + UI_ENTRY_ROW_GAP, y,
                         button_width, LV_SYMBOL_OK " Save", confirm_cb);
}
