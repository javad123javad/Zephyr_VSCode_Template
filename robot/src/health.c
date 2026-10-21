/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware watchdog fed only while the watched threads make progress, so a
 * hang or deadlock resets the robot. After a firmware update that never got
 * confirmed, that reset also makes MCUboot return to the previous image.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "health.h"

LOG_MODULE_REGISTER(health, LOG_LEVEL_INF);

#define WDT_TIMEOUT_MS   4000U
#define CHECK_PERIOD_MS  1000
/* Below every thread it watches, so a thread hogging the CPU starves it too. */
#define HEALTH_PRIORITY  K_PRIO_PREEMPT(14)
#define HEALTH_STACK     1024

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

static atomic_t beats[HEALTH_COUNT];
static atomic_t watched;

static const char *const names[HEALTH_COUNT] = {
	[HEALTH_LINK] = "link",
	[HEALTH_IMU] = "IMU",
};

void health_beat(enum health_source src)
{
	(void)atomic_inc(&beats[src]);
}

void health_watch(enum health_source src, bool on)
{
	if (on) {
		(void)atomic_or(&watched, BIT(src));
	} else {
		(void)atomic_and(&watched, ~BIT(src));
	}
}

static void health_thread(void *p1, void *p2, void *p3)
{
	struct wdt_timeout_cfg cfg = {
		.window.max = WDT_TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};
	atomic_val_t last[HEALTH_COUNT] = {0};
	int channel;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(wdt)) {
		LOG_ERR("Watchdog not ready");
		return;
	}

	channel = wdt_install_timeout(wdt, &cfg);
	if ((channel < 0) || (wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0)) {
		LOG_ERR("Watchdog setup failed (%d)", channel);
		return;
	}

	LOG_INF("Watchdog armed, %u ms", WDT_TIMEOUT_MS);

	for (;;) {
		bool alive = true;

		k_sleep(K_MSEC(CHECK_PERIOD_MS));

		for (size_t i = 0U; i < HEALTH_COUNT; i++) {
			atomic_val_t now = atomic_get(&beats[i]);

			if (((atomic_get(&watched) & BIT(i)) != 0) && (now == last[i])) {
				/* printk, not the deferred log: the reset may come first. */
				printk("health: %s thread stalled\n", names[i]);
				alive = false;
			}
			last[i] = now;
		}

		if (alive) {
			(void)wdt_feed(wdt, channel);
		}
	}
}

K_THREAD_DEFINE(health_tid, HEALTH_STACK, health_thread, NULL, NULL, NULL, HEALTH_PRIORITY, 0, 0);
