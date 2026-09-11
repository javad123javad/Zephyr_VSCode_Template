/*
 * Two MIC2026-1YM USB port power switches (one per USB port):
 *   USB1: EN on PE1, OCS/FLAG# on PB5
 *   USB2: EN on PE3, OCS/FLAG# on PB14
 *
 * EN is active-high (drive high to turn on the 5V switch). OCS/FLAG#
 * is an active-low, open-drain fault output (asserted on overcurrent
 * or thermal shutdown) - there's no direct "5V present" sense pin on
 * this chip, so "EN asserted and OCS not asserted" is the closest
 * available signal that power is actually flowing without a fault.
 *
 * Both signals are plain GPIOs with no chip/protocol driver involved,
 * so they're exposed via the standard zephyr,user devicetree node
 * rather than a custom binding.
 *
 * USB1 shares its physical connector with usbotg_hs1, which
 * usb_device_test.c exercises as a USB device plugged into a PC. The
 * PC supplies its own VBUS on that same pin, so USB1's EN is held
 * off here (not just left floating) rather than enabled, to avoid
 * two 5V sources fighting on one wire. Only USB2 is actually power-
 * switched and fault-monitored.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_power_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

struct usb_port {
	const char *name;
	struct gpio_dt_spec en;
	struct gpio_dt_spec ocs;
	bool enable_5v; /* false for USB1 - see file comment */
	bool fault;     /* last known state; only logged on change */
};

static struct usb_port ports[] = {
	{
		.name = "USB1",
		.en = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), usb1_en_gpios),
		.ocs = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), usb1_ocs_gpios),
		.enable_5v = false,
	},
	{
		.name = "USB2",
		.en = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), usb2_en_gpios),
		.ocs = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), usb2_ocs_gpios),
		.enable_5v = true,
	},
};

void usb_power_test_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		struct usb_port *port = &ports[i];
		int ret, val;

		if (!gpio_is_ready_dt(&port->en) || !gpio_is_ready_dt(&port->ocs)) {
			printk("%s: GPIO not ready\n", port->name);
			continue;
		}

		/* Configure OCS first so it's already readable the instant
		 * EN goes active.
		 */
		ret = gpio_pin_configure_dt(&port->ocs, GPIO_INPUT | GPIO_PULL_UP);
		if (ret < 0) {
			printk("%s: failed to configure OCS pin (%d)\n", port->name, ret);
			continue;
		}

		ret = gpio_pin_configure_dt(&port->en, port->enable_5v ?
					    GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			printk("%s: failed to configure EN (%d)\n", port->name, ret);
			continue;
		}

		if (!port->enable_5v) {
			printk("%s: 5V held off (shares a connector with a USB device "
			       "test)\n", port->name);
			continue;
		}

		val = gpio_pin_get_dt(&port->ocs);
		port->fault = val > 0;
		printk("%s: 5V enabled, %s\n", port->name,
		       val < 0 ? "OCS read failed" :
		       port->fault ? "FAULT (overcurrent/thermal)" : "OK");
	}
}

void usb_power_test_step(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		struct usb_port *port = &ports[i];
		int val;
		bool fault;

		if (!port->enable_5v || !gpio_is_ready_dt(&port->ocs)) {
			continue;
		}

		val = gpio_pin_get_dt(&port->ocs);
		if (val < 0) {
			continue;
		}

		fault = val > 0;
		if (fault != port->fault) {
			port->fault = fault;
			printk("%s: %s\n", port->name,
			       fault ? "FAULT (overcurrent/thermal)" : "OK, 5V present");
		}
	}
}
