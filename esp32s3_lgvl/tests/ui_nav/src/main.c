/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <lvgl.h>

#include "ui_nav.h"

#define TEST_DISPLAY_WIDTH  480
#define TEST_DISPLAY_HEIGHT 320

/* More pushes than the stack's fixed capacity, to prove overflow is
 * handled (never crashes, never corrupts navigation) rather than just
 * exercising the in-capacity path.
 */
#define OVERFLOW_PUSH_COUNT 20U

static void *ui_nav_suite_setup(void)
{
    /* CONFIG_LV_Z_AUTO_INIT=n: no devicetree display is bound in this
     * test, so Zephyr's own LVGL init (which requires one) is disabled
     * and we call lv_init() directly instead.
     */
    lv_init();
    return NULL;
}

ZTEST_SUITE(ui_nav, NULL, ui_nav_suite_setup, NULL, NULL, NULL);

ZTEST(ui_nav, test_push_pop_returns_to_caller)
{
    lv_display_create(TEST_DISPLAY_WIDTH, TEST_DISPLAY_HEIGHT);

    lv_obj_t *screen_a = lv_obj_create(NULL);
    lv_obj_t *screen_b = lv_obj_create(NULL);
    lv_obj_t *screen_c = lv_obj_create(NULL);

    nav_load_screen(screen_a);
    zassert_equal_ptr(lv_screen_active(), screen_a, "nav_load_screen must not touch the stack");

    nav_push_screen(screen_b);
    zassert_equal_ptr(lv_screen_active(), screen_b, "push must load the new screen");

    nav_push_screen(screen_c);
    zassert_equal_ptr(lv_screen_active(), screen_c, "a second push must load on top of the first");

    nav_pop_screen(NULL);
    zassert_equal_ptr(lv_screen_active(), screen_b,
                       "first pop must return to the screen pushed from");

    nav_pop_screen(NULL);
    zassert_equal_ptr(lv_screen_active(), screen_a,
                       "second pop must return to the original screen");

    nav_pop_screen(NULL);
    zassert_equal_ptr(lv_screen_active(), screen_a,
                       "popping an empty stack must be a no-op, not change the active screen");
}

ZTEST(ui_nav, test_push_overflow_does_not_crash)
{
    lv_display_create(TEST_DISPLAY_WIDTH, TEST_DISPLAY_HEIGHT);

    lv_obj_t *base = lv_obj_create(NULL);
    lv_obj_t *screens[OVERFLOW_PUSH_COUNT];

    nav_load_screen(base);

    for (uint32_t i = 0U; i < OVERFLOW_PUSH_COUNT; i++) {
        screens[i] = lv_obj_create(NULL);
        nav_push_screen(screens[i]);
    }

    zassert_equal_ptr(lv_screen_active(), screens[OVERFLOW_PUSH_COUNT - 1U],
                      "the most recent push must always load, even once the stack is full");

    for (uint32_t i = 0U; i < OVERFLOW_PUSH_COUNT; i++) {
        nav_pop_screen(NULL);
    }

    lv_obj_t *settled_screen = lv_screen_active();

    nav_pop_screen(NULL);
    zassert_equal_ptr(lv_screen_active(), settled_screen,
                      "once recorded history is exhausted, further pops must be no-ops");
}
