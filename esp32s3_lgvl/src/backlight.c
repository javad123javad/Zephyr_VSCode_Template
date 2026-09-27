/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>

#include <lvgl.h>

#include "backlight.h"

#define BACKLIGHT_LED_INDEX        0U
#define BACKLIGHT_FULL_PERCENT     100U
#define BACKLIGHT_DIM_PERCENT      15U
#define BACKLIGHT_IDLE_TIMEOUT_MS  30000U

/* native_sim has no PWM backlight node - guarded out there rather than a
 * hard compile error on the missing devicetree label.
 */
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(backlight_leds))

static const struct device *const backlight_dev = DEVICE_DT_GET(DT_NODELABEL(backlight_leds));
static bool backlight_dimmed;

void backlight_init(void)
{
    if (!device_is_ready(backlight_dev)) {
        return;
    }

    led_set_brightness(backlight_dev, BACKLIGHT_LED_INDEX, BACKLIGHT_FULL_PERCENT);
    backlight_dimmed = false;
}

void backlight_tick(void)
{
    if (!device_is_ready(backlight_dev)) {
        return;
    }

    uint32_t idle_ms = lv_display_get_inactive_time(NULL);

    if (idle_ms >= BACKLIGHT_IDLE_TIMEOUT_MS && !backlight_dimmed) {
        led_set_brightness(backlight_dev, BACKLIGHT_LED_INDEX, BACKLIGHT_DIM_PERCENT);
        backlight_dimmed = true;
    } else if (idle_ms < BACKLIGHT_IDLE_TIMEOUT_MS && backlight_dimmed) {
        led_set_brightness(backlight_dev, BACKLIGHT_LED_INDEX, BACKLIGHT_FULL_PERCENT);
        backlight_dimmed = false;
    }
}

#else

void backlight_init(void)
{
}

void backlight_tick(void)
{
}

#endif
