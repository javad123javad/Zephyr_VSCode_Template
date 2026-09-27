/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_common.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_placeholder.h"
#include "ui_settings.h"

#define UI_MENU_TILE_WIDTH  190
#define UI_MENU_TILE_HEIGHT 100
#define UI_MENU_TILE_HGAP   64
#define UI_MENU_TILE_VGAP   20
#define UI_MENU_TILE_TOP    20

static lv_obj_t *scr_menu;

lv_obj_t *ui_menu_screen_get(void)
{
    return scr_menu;
}

static void menu_settings_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    nav_push_screen(ui_settings_screen_get());
}

static void menu_shell_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_placeholder_screen_enter("Shell");
}

static void menu_radio_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_placeholder_screen_enter("Radio");
}

static void menu_about_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_placeholder_screen_enter("About");
}

static void create_menu_tile(lv_obj_t *parent, int32_t x, int32_t y, const char *symbol,
                             const char *label_text, lv_event_cb_t cb)
{
    lv_obj_t *tile = lv_button_create(parent);

    lv_obj_set_size(tile, UI_MENU_TILE_WIDTH, UI_MENU_TILE_HEIGHT);
    lv_obj_set_pos(tile, x, y);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(tile);

    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_label_set_text(icon, symbol);

    lv_obj_t *label = lv_label_create(tile);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(label, label_text);
}

void ui_menu_screen_create(void)
{
    scr_menu = lv_obj_create(NULL);

    int32_t col0 = UI_EDGE_MARGIN;
    int32_t col1 = col0 + UI_MENU_TILE_WIDTH + UI_MENU_TILE_HGAP;
    int32_t row0 = UI_MENU_TILE_TOP;
    int32_t row1 = row0 + UI_MENU_TILE_HEIGHT + UI_MENU_TILE_VGAP;

    create_menu_tile(scr_menu, col0, row0, LV_SYMBOL_SETTINGS, "Settings",
                     menu_settings_click_cb);
    create_menu_tile(scr_menu, col1, row0, LV_SYMBOL_KEYBOARD, "Shell", menu_shell_click_cb);
    create_menu_tile(scr_menu, col0, row1, LV_SYMBOL_AUDIO, "Radio", menu_radio_click_cb);
    create_menu_tile(scr_menu, col1, row1, LV_SYMBOL_LIST, "About", menu_about_click_cb);

    lv_obj_t *back_btn = lv_button_create(scr_menu);

    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -UI_EDGE_MARGIN / 2);
    lv_obj_add_event_cb(back_btn, nav_pop_screen, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);

    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
}
