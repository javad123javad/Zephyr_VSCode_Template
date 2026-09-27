/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "ui_home.h"
#include "ui_nav.h"
#include "ui_password.h"
#include "wifi.h"

static lv_obj_t *scr_password;
static lv_obj_t *password_ta;
static lv_obj_t *password_kb;
static lv_obj_t *password_eye_label;

static char pending_ssid[33];
static uint8_t pending_ssid_len;

lv_obj_t *ui_password_screen_get(void)
{
    return scr_password;
}

void ui_password_screen_enter(const char *ssid, uint8_t ssid_len)
{
    memcpy(pending_ssid, ssid, ssid_len);
    pending_ssid[ssid_len] = '\0';
    pending_ssid_len = ssid_len;

    lv_textarea_set_placeholder_text(password_ta, pending_ssid);
    lv_textarea_set_text(password_ta, "");
    nav_push_screen(scr_password);
}

static void password_eye_click_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    bool hidden = lv_textarea_get_password_mode(password_ta);

    lv_textarea_set_password_mode(password_ta, !hidden);
    lv_label_set_text(password_eye_label, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void password_confirm(void)
{
    const char *password = lv_textarea_get_text(password_ta);

    wifi_connect_and_save(pending_ssid, pending_ssid_len, true, password, strlen(password));
    nav_load_screen(ui_home_screen_get());
}

static void password_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        password_confirm();
    } else if (code == LV_EVENT_CANCEL) {
        nav_pop_screen(e);
    }
}

void ui_password_screen_create(void)
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
