/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "ui_home.h"
#include "ui_nav.h"
#include "ui_network.h"
#include "ui_password.h"
#include "wifi.h"

/* How many scan results are shown per page: the list area comfortably fits
 * 3 full-height entry rows without needing scroll.
 */
#define WIFI_RESULTS_PER_PAGE 3

static lv_obj_t *scr_network;
static lv_obj_t *wifi_scan_status_label;
static lv_obj_t *wifi_results;
static lv_obj_t *wifi_page_label;
static int32_t wifi_page;
static int32_t wifi_last_total;
static bool wifi_scanning;

lv_obj_t *ui_network_screen_get(void)
{
    return scr_network;
}

static void render_wifi_page(void);

static bool wifi_entry_is_active(const struct wifi_scan_entry *entry)
{
    if (wifi_get_status() == WIFI_STATUS_CONNECTING) {
        return false;
    }

    const char *active_ssid = wifi_get_ssid();

    return strlen(active_ssid) == entry->ssid_len &&
           memcmp(active_ssid, entry->ssid, entry->ssid_len) == 0;
}

static void wifi_rescan_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    lv_obj_clean(wifi_results);
    wifi_page = 0;
    wifi_last_total = 0;
    wifi_scanning = true;
    lv_label_set_text(wifi_scan_status_label, "Scanning...");
    lv_label_set_text(wifi_page_label, "");
    wifi_scan_start();
}

static void wifi_back_click_cb(lv_event_t *e)
{
    wifi_resume_reconnect();
    nav_pop_screen(e);
}

static void wifi_result_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int32_t index = (int32_t)(intptr_t)lv_obj_get_user_data(btn);
    struct wifi_scan_entry entry;

    if (!wifi_scan_get_entry(index, &entry)) {
        return;
    }

    if (!entry.secured) {
        wifi_connect_and_save(entry.ssid, entry.ssid_len, false, NULL, 0);
        nav_load_screen(ui_home_screen_get());
        return;
    }

    ui_password_screen_enter(entry.ssid, entry.ssid_len);
}

static void wifi_forget_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int32_t index = (int32_t)(intptr_t)lv_obj_get_user_data(btn);
    struct wifi_scan_entry entry;

    if (!wifi_scan_get_entry(index, &entry)) {
        return;
    }

    wifi_forget(entry.ssid, entry.ssid_len);
    render_wifi_page();
}

static void wifi_forget_all_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    wifi_forget_all();
    render_wifi_page();
}

static void render_wifi_page(void)
{
    int32_t total = wifi_scan_get_count();
    int32_t total_pages = (total + WIFI_RESULTS_PER_PAGE - 1) / WIFI_RESULTS_PER_PAGE;

    if (total_pages < 1) {
        total_pages = 1;
    }
    if (wifi_page >= total_pages) {
        wifi_page = total_pages - 1;
    }
    if (wifi_page < 0) {
        wifi_page = 0;
    }

    lv_obj_clean(wifi_results);

    int32_t start = wifi_page * WIFI_RESULTS_PER_PAGE;
    int32_t end = (start + WIFI_RESULTS_PER_PAGE < total) ? start + WIFI_RESULTS_PER_PAGE : total;

    for (int32_t i = start; i < end; i++) {
        struct wifi_scan_entry entry;

        if (!wifi_scan_get_entry(i, &entry)) {
            break;
        }

        bool active = wifi_entry_is_active(&entry);
        bool saved = wifi_is_saved(entry.ssid, entry.ssid_len);

        lv_obj_t *row = lv_obj_create(wifi_results);

        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);

        lv_obj_t *btn = lv_button_create(row);

        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, 50);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, wifi_result_click_cb, LV_EVENT_CLICKED, NULL);
        if (active) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        } else if (saved) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        }

        lv_obj_t *label = lv_label_create(btn);

        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_width(label, LV_PCT(90));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_fmt(label, "%s%s", entry.secured ? LV_SYMBOL_CLOSE " " : "",
                              entry.ssid);

        if (saved) {
            lv_obj_t *forget_btn = lv_button_create(row);

            lv_obj_set_size(forget_btn, 50, 50);
            lv_obj_set_style_bg_color(forget_btn, lv_palette_main(LV_PALETTE_RED),
                                       LV_PART_MAIN);
            lv_obj_set_user_data(forget_btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(forget_btn, wifi_forget_click_cb, LV_EVENT_CLICKED, NULL);

            lv_obj_t *forget_label = lv_label_create(forget_btn);

            lv_obj_set_style_text_font(forget_label, &lv_font_montserrat_24, LV_PART_MAIN);
            lv_obj_center(forget_label);
            lv_label_set_text(forget_label, LV_SYMBOL_CLOSE);
        }
    }

    lv_label_set_text_fmt(wifi_page_label, "Page %d/%d", wifi_page + 1, total_pages);
}

static void wifi_prev_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (wifi_page > 0) {
        wifi_page--;
        render_wifi_page();
    }
}

static void wifi_next_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    int32_t total = wifi_scan_get_count();
    int32_t total_pages = (total + WIFI_RESULTS_PER_PAGE - 1) / WIFI_RESULTS_PER_PAGE;

    if (wifi_page + 1 < total_pages) {
        wifi_page++;
        render_wifi_page();
    }
}

void ui_network_screen_enter(void)
{
    lv_label_set_text_fmt(wifi_scan_status_label, "Connected: %s", wifi_get_ssid());
    wifi_pause_reconnect();
    nav_push_screen(scr_network);
}

void ui_network_poll_scan(void)
{
    if (!wifi_scanning) {
        return;
    }

    int32_t count = wifi_scan_get_count();

    if (count != wifi_last_total) {
        wifi_last_total = count;
        render_wifi_page();
    }

    if (wifi_scan_is_done()) {
        wifi_scanning = false;
        lv_label_set_text_fmt(wifi_scan_status_label, "%d network%s found", count,
                              count == 1 ? "" : "s");
    }
}

void ui_network_screen_create(void)
{
    scr_network = lv_obj_create(NULL);

    lv_obj_t *back_btn = lv_button_create(scr_network);

    lv_obj_set_size(back_btn, 130, 50);
    lv_obj_set_pos(back_btn, 16, 16);
    lv_obj_add_event_cb(back_btn, wifi_back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);

    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");

    lv_obj_t *rescan_btn = lv_button_create(scr_network);

    lv_obj_set_size(rescan_btn, 110, 50);
    lv_obj_align(rescan_btn, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_event_cb(rescan_btn, wifi_rescan_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *rescan_label = lv_label_create(rescan_btn);

    lv_obj_set_style_text_font(rescan_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(rescan_label);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH " Rescan");

    lv_obj_t *forget_all_btn = lv_button_create(scr_network);

    lv_obj_set_size(forget_all_btn, 110, 50);
    lv_obj_align(forget_all_btn, LV_ALIGN_TOP_RIGHT, -134, 16);
    lv_obj_set_style_bg_color(forget_all_btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_add_event_cb(forget_all_btn, wifi_forget_all_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *forget_all_label = lv_label_create(forget_all_btn);

    lv_obj_set_style_text_font(forget_all_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(forget_all_label);
    lv_label_set_text(forget_all_label, "Forget All");

    wifi_scan_status_label = lv_label_create(scr_network);
    lv_obj_set_style_text_font(wifi_scan_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(wifi_scan_status_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(wifi_scan_status_label, "");

    wifi_results = lv_obj_create(scr_network);
    lv_obj_remove_flag(wifi_results, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wifi_results, 448, 176);
    lv_obj_set_pos(wifi_results, 16, 84);
    lv_obj_set_flex_flow(wifi_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_results, 10, LV_PART_MAIN);

    lv_obj_t *prev_btn = lv_button_create(scr_network);

    lv_obj_set_size(prev_btn, 100, 40);
    lv_obj_set_pos(prev_btn, 16, 268);
    lv_obj_add_event_cb(prev_btn, wifi_prev_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *prev_label = lv_label_create(prev_btn);

    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(prev_label);
    lv_label_set_text(prev_label, LV_SYMBOL_LEFT " Prev");

    wifi_page_label = lv_label_create(scr_network);
    lv_obj_set_style_text_font(wifi_page_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(wifi_page_label, LV_ALIGN_TOP_MID, 0, 278);
    lv_label_set_text(wifi_page_label, "");

    lv_obj_t *next_btn = lv_button_create(scr_network);

    lv_obj_set_size(next_btn, 100, 40);
    lv_obj_align(next_btn, LV_ALIGN_TOP_RIGHT, -16, 268);
    lv_obj_add_event_cb(next_btn, wifi_next_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_label = lv_label_create(next_btn);

    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(next_label);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
}
