/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <lvgl.h>

#include "ui_clock.h"

#define TEST_DISPLAY_WIDTH  480
#define TEST_DISPLAY_HEIGHT 320

/* Mirrors the real-world UTC offset range documented in ui_clock.c
 * (TZ_OFFSET_QUARTERS_MIN/MAX): -12:00 to +14:00, in quarter-hour steps.
 */
#define QUARTER_HOUR_SECONDS  900
#define TZ_OFFSET_MIN_SECONDS (-12 * 3600)
#define TZ_OFFSET_MAX_SECONDS (14 * 3600)

/* More clicks than the clamp range needs, to prove the clamp holds rather
 * than just landing on it by coincidence.
 */
#define CLICKS_PAST_UPPER_CLAMP 100U
#define CLICKS_PAST_LOWER_CLAMP 200U

static void click_button(lv_obj_t *btn, uint32_t times)
{
    for (uint32_t i = 0U; i < times; i++) {
        lv_obj_send_event(btn, LV_EVENT_CLICKED, NULL);
    }
}

static void *ui_clock_suite_setup(void)
{
    /* CONFIG_LV_Z_AUTO_INIT=n: no devicetree display is bound in this
     * test, so Zephyr's own LVGL init (which requires one) is disabled
     * and we call lv_init() directly instead.
     */
    lv_init();
    return NULL;
}

ZTEST_SUITE(ui_clock, NULL, ui_clock_suite_setup, NULL, NULL, NULL);

ZTEST(ui_clock, test_offset_seconds_step_and_clamp)
{
    lv_display_create(TEST_DISPLAY_WIDTH, TEST_DISPLAY_HEIGHT);
    ui_clock_screen_create();

    lv_obj_t *scr = ui_clock_screen_get();
    lv_obj_t *row = lv_obj_get_child(scr, 1);
    lv_obj_t *minus_btn = lv_obj_get_child(row, 0);
    lv_obj_t *plus_btn = lv_obj_get_child(row, 2);

    zassert_equal(ui_clock_get_offset_seconds(), 0, "default offset must be UTC");

    click_button(plus_btn, CLICKS_PAST_UPPER_CLAMP);
    zassert_equal(ui_clock_get_offset_seconds(), TZ_OFFSET_MAX_SECONDS,
                  "offset must clamp at +14:00 instead of wrapping or overflowing");

    click_button(minus_btn, CLICKS_PAST_LOWER_CLAMP);
    zassert_equal(ui_clock_get_offset_seconds(), TZ_OFFSET_MIN_SECONDS,
                  "offset must clamp at -12:00 instead of wrapping or underflowing");

    click_button(plus_btn, 1U);
    zassert_equal(ui_clock_get_offset_seconds(), TZ_OFFSET_MIN_SECONDS + QUARTER_HOUR_SECONDS,
                  "a single step off the lower clamp must still move by one quarter hour");
}
