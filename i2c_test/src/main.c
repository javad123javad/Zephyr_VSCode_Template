/*
 * Peripheral bring-up test for the mind,mindos_n6 board. See README
 * for what's wired where; each peripheral's test lives in its own
 * src/<name>_test.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "i2c_scan.h"
#include "pca9557_test.h"
#include "bme280_test.h"
#include "can_test.h"
#include "i2s_test.h" /* no API - runs its own thread once linked in */
#include "usb_power_test.h"
#include "usb_device_test.h" /* no API - runs its own thread once linked in */

int main(void)
{
	printk("mindos_n6 peripheral test\n");

	i2c_scan_all();

	pca9557_test_init();
	bme280_test_init();
	can_test_init();
	usb_power_test_init();

	/* 500ms tick: advance the PCA9557 pattern every tick, sample the
	 * BME280 every 4th tick (2s), drive the CAN test every 2nd tick
	 * (1s), and poll USB power fault status every tick. The I2S test
	 * runs independently in its own thread (see i2s_test.c).
	 */
	for (unsigned int tick = 0;; tick++) {
		pca9557_test_step();

		if (tick % 4 == 0) {
			bme280_test_sample();
		}

		can_test_step(tick);
		usb_power_test_step();

		k_msleep(500);
	}

	return 0;
}
