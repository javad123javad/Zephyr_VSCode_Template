/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>

#include "ui_nav.h"

/* Deepest the menu hierarchy goes today is Home -> Menu -> Settings ->
 * Network/Clock/Local/Calibrate, i.e. 4. Leaves headroom without needing
 * dynamic allocation.
 */
#define NAV_STACK_MAX_DEPTH 8U

static lv_obj_t *nav_stack[NAV_STACK_MAX_DEPTH];
static uint32_t nav_stack_depth;

void nav_load_screen(lv_obj_t *screen)
{
    /* No animation: on this SPI link a full-screen fade means several
     * full redraws, which is visibly slow. A single instant redraw is
     * much faster.
     */
    lv_screen_load(screen);
}

void nav_push_screen(lv_obj_t *screen)
{
    lv_obj_t *current = lv_screen_active();

    if (nav_stack_depth < ARRAY_SIZE(nav_stack)) {
        nav_stack[nav_stack_depth] = current;
        nav_stack_depth++;
    }

    nav_load_screen(screen);
}

void nav_pop_screen(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (nav_stack_depth == 0U) {
        return;
    }

    nav_stack_depth--;
    nav_load_screen(nav_stack[nav_stack_depth]);
}
