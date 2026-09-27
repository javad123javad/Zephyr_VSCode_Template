/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>

#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(internet_clock, LOG_LEVEL_INF);

#include "backlight.h"
#include "ui.h"
#include "wifi.h"

/* PSRAM init only logs on failure (esp_init_psram(), soc/espressif/
 * common/esp_psram.c); log success too so it shows up on the console
 * instead of only being visible via the linker's ext_dram_seg size.
 */
#if defined(CONFIG_ESP_SPIRAM)
#include <esp_psram.h>
#endif

int main(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int64_t last_update = 0;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return 0;
	}
	LOG_INF("Display device ready");

#if defined(CONFIG_ESP_SPIRAM)
	if (esp_psram_is_initialized()) {
		LOG_INF("PSRAM ready: %zu bytes", esp_psram_get_size());
	} else {
		LOG_ERR("PSRAM not initialized");
	}
#endif

	backlight_init();
	ui_init();
	lv_timer_handler();
	display_blanking_off(display_dev);

	while (1) {
		int64_t now = k_uptime_get();

		if (now - last_update >= MSEC_PER_SEC) {
			last_update = now;
			ui_tick(wifi_current_epoch_s(), wifi_get_status());
		}

		ui_poll_wifi_scan();
		backlight_tick();
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}

	return 0;
}
