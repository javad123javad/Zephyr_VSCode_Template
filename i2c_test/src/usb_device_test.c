/*
 * USB1 (usbotg_hs1 / zephyr_udc0) as a USB CDC-ACM serial device -
 * the simplest available way to test this board's USB wiring, since
 * Zephyr has no host-mode (UHC) driver for the STM32N6 OTG HS
 * peripheral: enumerating as a device is the only thing the stack can
 * actually do here today.
 *
 * This shares the same physical connector as the MIC2026-switched 5V
 * handled in usb_power_test.c, which therefore holds USB1's EN off so
 * the board's own 5V and the PC's VBUS don't fight on the same pin.
 *
 * The USB device stack is brought up on the first test run. The test
 * passes once a host has enumerated and configured the device; after
 * that, anything typed into the enumerated serial port is echoed
 * straight back, which confirms data transfer in both directions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_device_test.h"

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>

#define USB_TEST_TIMEOUT_MS 10000
#define USB_TEST_POLL_MS    100

static const struct device *const cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static struct usbd_context *usbd;
static atomic_t usb_configured;

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(ctx);
			atomic_set(&usb_configured, 0);
		}
	}

	if (msg->type == USBD_MSG_RESET) {
		atomic_set(&usb_configured, 0);
	}

	/* status is the configuration value selected by the host, 0 = none */
	if (msg->type == USBD_MSG_CONFIGURATION) {
		atomic_set(&usb_configured, (msg->status != 0) ? 1 : 0);
	}
}

/* Echoes every received byte straight back. Runs in ISR context, so
 * uart_poll_out()'s brief busy-wait per byte is fine for a manual
 * typing test but would be inappropriate for high-throughput use.
 */
static void echo_isr(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	while (uart_irq_is_pending(dev) > 0) {
		if (uart_irq_rx_ready(dev)) {
			while (uart_fifo_read(dev, &c, 1) == 1) {
				uart_poll_out(dev, c);
			}
		}
		uart_irq_update(dev);
	}
}

static int usb_setup(const struct shell *sh)
{
	if (usbd != NULL) {
		return 0;
	}

	if (!device_is_ready(cdc_dev)) {
		shell_error(sh, "USB1: CDC-ACM device not ready");
		return -ENODEV;
	}

	usbd = sample_usbd_init_device(usbd_msg_cb);
	if (usbd == NULL) {
		shell_error(sh, "USB1: failed to init USB device stack");
		return -EIO;
	}

	uart_irq_callback_set(cdc_dev, echo_isr);
	uart_irq_rx_enable(cdc_dev);

	if (!usbd_can_detect_vbus(usbd)) {
		int ret = usbd_enable(usbd);

		if (ret != 0) {
			shell_error(sh, "USB1: failed to enable USB device support (%d)", ret);
			return ret;
		}
	}

	shell_print(sh, "USB1 @ usbotg_hs1: USB device stack enabled");
	return 0;
}

int usb_device_test_run(const struct shell *sh)
{
	int ret = usb_setup(sh);

	if (ret != 0) {
		return ret;
	}

	if (atomic_get(&usb_configured) == 0) {
		shell_print(sh, "USB1: waiting up to %d s for a host to enumerate the device...",
			    USB_TEST_TIMEOUT_MS / 1000);
	}

	for (int waited = 0; atomic_get(&usb_configured) == 0; waited += USB_TEST_POLL_MS) {
		if (waited >= USB_TEST_TIMEOUT_MS) {
			shell_error(sh, "USB1: not enumerated - is it plugged into a host? Use a "
				    "USB-A to USB-A cable or a hub (see README)");
			return -ETIMEDOUT;
		}
		k_msleep(USB_TEST_POLL_MS);
	}

	shell_print(sh, "USB1: enumerated and configured by the host; open the serial port "
		    "and type to test echo");
	return 0;
}
