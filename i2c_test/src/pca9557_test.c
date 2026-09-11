/*
 * PCA9557PW,118 8-bit GPIO expander on io_i2c @ 0x19 (address pins:
 * A0=VCC, A1=GND, A2=GND). Runs a walking-bit pattern on all 8 pins
 * via the out-of-tree "nxp,pca9557" driver (see ../pca9557_driver)
 * and Zephyr's standard gpio.h API - nothing is logged per step, to
 * keep the console usable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pca9557_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define PCA9557_NPINS 8

static const struct device *expander = DEVICE_DT_GET(DT_NODELABEL(io_expander));
static bool pca9557_ready;

void pca9557_test_init(void)
{
	int ret;

	if (!device_is_ready(expander)) {
		printk("PCA9557: device not ready\n");
		return;
	}

	for (uint8_t pin = 0; pin < PCA9557_NPINS; pin++) {
		ret = gpio_pin_configure(expander, pin, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			printk("PCA9557: failed to configure pin %u (%d)\n", pin, ret);
			return;
		}
	}

	printk("PCA9557 @ io_i2c (0x19): walking-bit pattern running (silent)\n");
	pca9557_ready = true;
}

void pca9557_test_step(void)
{
	static uint8_t pin;
	int ret;

	if (!pca9557_ready) {
		return;
	}

	ret = gpio_port_set_masked_raw(expander, BIT_MASK(PCA9557_NPINS), BIT(pin));
	if (ret < 0) {
		printk("PCA9557: write failed (%d)\n", ret);
	}

	pin = (pin + 1) % PCA9557_NPINS;
}
