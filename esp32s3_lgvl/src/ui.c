/*
 * SPDX-License-Identifier: Apache-2.0
 */

/* gmtime_r needs a POSIX feature-test macro on native_sim's host glibc
 * (built with -std=c17); Zephyr's own libc doesn't need this but tolerates
 * it.
 */
#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "ui.h"
#include "wifi.h"

/* Change to your local UTC offset; also adjustable live from the Time zone menu. */
#define DEFAULT_TZ_OFFSET_HOURS 0

/* Clock screen */
static lv_obj_t *scr_clock;
static lv_obj_t *wifi_status_label;
static lv_obj_t *weekday_label;
static lv_obj_t *time_label;
static lv_obj_t *date_label;

/* Menu screen */
static lv_obj_t *scr_menu;

/* Time zone screen */
static lv_obj_t *scr_tz;
static lv_obj_t *tz_spinbox;

/* Wi-Fi screen */
#define WIFI_RESULTS_PER_PAGE 3

static lv_obj_t *scr_wifi;
static lv_obj_t *wifi_scan_status_label;
static lv_obj_t *wifi_results;
static lv_obj_t *wifi_page_label;
static int wifi_page;
static int wifi_last_total;
static bool wifi_scanning;

/* Password entry screen */
static lv_obj_t *scr_password;
static lv_obj_t *password_ta;
static lv_obj_t *password_kb;
static char pending_ssid[33];
static uint8_t pending_ssid_len;

static void switch_screen(lv_obj_t *screen)
{
    /* No animation: on this SPI link a full-screen fade means several
     * full redraws, which is visibly slow. A single instant redraw is
     * much faster.
     */
    lv_screen_load(screen);
}

static lv_obj_t *add_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_set_size(btn, LV_PCT(100), 56);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);
    lv_label_set_text(label, text);

    return btn;
}

static void clock_settings_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    switch_screen(scr_menu);
}

static void menu_tz_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    switch_screen(scr_tz);
}

static void menu_wifi_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    lv_label_set_text_fmt(wifi_scan_status_label, "Connected: %s", wifi_get_ssid());
    wifi_pause_reconnect();
    switch_screen(scr_wifi);
}

static void menu_back_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    switch_screen(scr_clock);
}

static void tz_minus_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    lv_spinbox_decrement(tz_spinbox);
}

static void tz_plus_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    lv_spinbox_increment(tz_spinbox);
}

static void tz_back_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    switch_screen(scr_menu);
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
    ARG_UNUSED(e);
    wifi_resume_reconnect();
    switch_screen(scr_menu);
}

static void wifi_result_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    struct wifi_scan_entry entry;

    if (!wifi_scan_get_entry(index, &entry)) {
        return;
    }

    if (!entry.secured) {
        wifi_connect_and_save(entry.ssid, entry.ssid_len, false, NULL, 0);
        switch_screen(scr_clock);
        return;
    }

    memcpy(pending_ssid, entry.ssid, entry.ssid_len);
    pending_ssid[entry.ssid_len] = '\0';
    pending_ssid_len = entry.ssid_len;

    lv_textarea_set_placeholder_text(password_ta, pending_ssid);
    lv_textarea_set_text(password_ta, "");
    switch_screen(scr_password);
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

static void wifi_forget_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    struct wifi_scan_entry entry;

    if (!wifi_scan_get_entry(index, &entry)) {
        return;
    }

    wifi_forget(entry.ssid, entry.ssid_len);
    render_wifi_page();
}

static void render_wifi_page(void)
{
    int total = wifi_scan_get_count();
    int total_pages = (total + WIFI_RESULTS_PER_PAGE - 1) / WIFI_RESULTS_PER_PAGE;

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

    int start = wifi_page * WIFI_RESULTS_PER_PAGE;
    int end = (start + WIFI_RESULTS_PER_PAGE < total) ? start + WIFI_RESULTS_PER_PAGE : total;

    for (int i = start; i < end; i++) {
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

    int total = wifi_scan_get_count();
    int total_pages = (total + WIFI_RESULTS_PER_PAGE - 1) / WIFI_RESULTS_PER_PAGE;

    if (wifi_page + 1 < total_pages) {
        wifi_page++;
        render_wifi_page();
    }
}

static void password_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        const char *password = lv_textarea_get_text(password_ta);

        wifi_connect_and_save(pending_ssid, pending_ssid_len, true, password,
                              strlen(password));
        switch_screen(scr_clock);
    } else if (code == LV_EVENT_CANCEL) {
        switch_screen(scr_wifi);
    }
}

static void build_clock_screen(void)
{
    scr_clock = lv_obj_create(NULL);

    lv_obj_t *settings_btn = lv_button_create(scr_clock);

    lv_obj_set_size(settings_btn, 64, 64);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_event_cb(settings_btn, clock_settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_label = lv_label_create(settings_btn);

    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(settings_label);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);

    wifi_status_label = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_label_set_text(wifi_status_label, "Connecting...");

    time_label = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(time_label, "--:--:--");

    weekday_label = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(weekday_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(weekday_label, LV_ALIGN_CENTER, 0, 50);
    lv_label_set_text(weekday_label, "---");

    date_label = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 84);
    lv_label_set_text(date_label, "--- --");
}

static void build_menu_screen(void)
{
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_menu, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr_menu, 32, LV_PART_MAIN);

    lv_obj_t *tz_btn = add_button(scr_menu, "Time Zone", menu_tz_click_cb);

    lv_obj_set_width(tz_btn, 300);

    lv_obj_t *wifi_btn = add_button(scr_menu, "Wi-Fi Setup", menu_wifi_click_cb);

    lv_obj_set_width(wifi_btn, 300);

    lv_obj_t *back_btn = add_button(scr_menu, "Back", menu_back_click_cb);

    lv_obj_set_width(back_btn, 300);
}

static void build_tz_screen(void)
{
    scr_tz = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_tz, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_tz, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr_tz, 24, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr_tz);

    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "UTC offset (hours)");

    lv_obj_t *row = lv_obj_create(scr_tz);

    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 80);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *minus_btn = lv_button_create(row);

    lv_obj_set_size(minus_btn, 70, 70);
    lv_obj_add_event_cb(minus_btn, tz_minus_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *minus_label = lv_label_create(minus_btn);

    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(minus_label);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);

    tz_spinbox = lv_spinbox_create(row);
    lv_spinbox_set_range(tz_spinbox, -12, 14);
    lv_spinbox_set_digit_format(tz_spinbox, 2, 0);
    lv_spinbox_set_value(tz_spinbox, DEFAULT_TZ_OFFSET_HOURS);
    lv_obj_set_style_text_font(tz_spinbox, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_size(tz_spinbox, 100, 60);

    lv_obj_t *plus_btn = lv_button_create(row);

    lv_obj_set_size(plus_btn, 70, 70);
    lv_obj_add_event_cb(plus_btn, tz_plus_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *plus_label = lv_label_create(plus_btn);

    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(plus_label);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);

    lv_obj_t *back_btn = add_button(scr_tz, "Back", tz_back_click_cb);

    lv_obj_set_width(back_btn, 300);
}

static void build_wifi_screen(void)
{
    scr_wifi = lv_obj_create(NULL);

    lv_obj_t *back_btn = lv_button_create(scr_wifi);

    lv_obj_set_size(back_btn, 130, 50);
    lv_obj_set_pos(back_btn, 16, 16);
    lv_obj_add_event_cb(back_btn, wifi_back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);

    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");

    lv_obj_t *rescan_btn = lv_button_create(scr_wifi);

    lv_obj_set_size(rescan_btn, 150, 50);
    lv_obj_align(rescan_btn, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_event_cb(rescan_btn, wifi_rescan_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *rescan_label = lv_label_create(rescan_btn);

    lv_obj_set_style_text_font(rescan_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(rescan_label);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH " Rescan");

    wifi_scan_status_label = lv_label_create(scr_wifi);
    lv_obj_set_style_text_font(wifi_scan_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(wifi_scan_status_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(wifi_scan_status_label, "");

    wifi_results = lv_obj_create(scr_wifi);
    lv_obj_remove_flag(wifi_results, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wifi_results, 448, 176);
    lv_obj_set_pos(wifi_results, 16, 84);
    lv_obj_set_flex_flow(wifi_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_results, 10, LV_PART_MAIN);

    lv_obj_t *prev_btn = lv_button_create(scr_wifi);

    lv_obj_set_size(prev_btn, 100, 40);
    lv_obj_set_pos(prev_btn, 16, 268);
    lv_obj_add_event_cb(prev_btn, wifi_prev_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *prev_label = lv_label_create(prev_btn);

    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(prev_label);
    lv_label_set_text(prev_label, LV_SYMBOL_LEFT " Prev");

    wifi_page_label = lv_label_create(scr_wifi);
    lv_obj_set_style_text_font(wifi_page_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(wifi_page_label, LV_ALIGN_TOP_MID, 0, 278);
    lv_label_set_text(wifi_page_label, "");

    lv_obj_t *next_btn = lv_button_create(scr_wifi);

    lv_obj_set_size(next_btn, 100, 40);
    lv_obj_align(next_btn, LV_ALIGN_TOP_RIGHT, -16, 268);
    lv_obj_add_event_cb(next_btn, wifi_next_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_label = lv_label_create(next_btn);

    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(next_label);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
}

static lv_obj_t *password_eye_label;

static void password_eye_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    bool hidden = lv_textarea_get_password_mode(password_ta);

    lv_textarea_set_password_mode(password_ta, !hidden);
    lv_label_set_text(password_eye_label, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void build_password_screen(void)
{
    scr_password = lv_obj_create(NULL);
    lv_obj_remove_flag(scr_password, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr_password, 8, LV_PART_MAIN);

    password_ta = lv_textarea_create(scr_password);
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_password_mode(password_ta, true);
    lv_obj_set_style_text_font(password_ta, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_size(password_ta, 400, 50);
    lv_obj_set_pos(password_ta, 8, 8);

    lv_obj_t *eye_btn = lv_button_create(scr_password);

    lv_obj_set_size(eye_btn, 56, 50);
    lv_obj_set_pos(eye_btn, 416, 8);
    lv_obj_add_event_cb(eye_btn, password_eye_click_cb, LV_EVENT_CLICKED, NULL);

    password_eye_label = lv_label_create(eye_btn);
    lv_obj_set_style_text_font(password_eye_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(password_eye_label);
    lv_label_set_text(password_eye_label, LV_SYMBOL_EYE_CLOSE);

    password_kb = lv_keyboard_create(scr_password);
    lv_keyboard_set_textarea(password_kb, password_ta);
    lv_obj_set_size(password_kb, 480, 220);
    lv_obj_set_pos(password_kb, 0, 2);
    lv_obj_add_event_cb(password_kb, password_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(password_kb, password_kb_event_cb, LV_EVENT_CANCEL, NULL);
}

void ui_init(void)
{
    build_clock_screen();
    build_menu_screen();
    build_tz_screen();
    build_wifi_screen();
    build_password_screen();

    lv_screen_load(scr_clock);
}

void ui_tick(int64_t epoch_s, enum wifi_net_status status)
{
    static const char *const weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    switch (status) {
    case WIFI_STATUS_CONNECTING:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WARNING " Connecting...");
        break;
    case WIFI_STATUS_CONNECTED:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_REFRESH " Syncing time...");
        break;
    case WIFI_STATUS_TIME_SYNCED:
        lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI " Connected");
        break;
    }

    if (epoch_s == 0) {
        return;
    }

    time_t local_time = (time_t)(epoch_s + lv_spinbox_get_value(tz_spinbox) * 3600);
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

void ui_poll_wifi_scan(void)
{
    if (!wifi_scanning) {
        return;
    }

    int count = wifi_scan_get_count();

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
