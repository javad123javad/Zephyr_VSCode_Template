/*
 * USB1 (usbotg_hs1 / zephyr_udc0) as a USB CDC-ACM serial device -
 * the simplest available way to test this board's USB wiring, since
 * Zephyr has no host-mode (UHC) driver for the STM32N6 OTG HS
 * peripheral: enumerating as a device is the only thing the stack can
 * actually do here today.
 *
 * IMPORTANT: this shares the same physical connector as the
 * MIC2026-switched 5V handled in usb_power_test.c, which always
 * drives that port's EN pin high at boot. Before plugging this port
 * into a PC, physically isolate/unpower the MIC2026 (per the user's
 * own call - see the conversation this was added in) so the board's
 * own 5V output and the PC's VBUS don't fight on the same pin.
 *
 * Runs in its own thread so it doesn't block the rest of bring-up
 * waiting for a PC to open the port. Once a terminal opens the
 * enumerated serial port (DTR asserted), anything typed is echoed
 * straight back - confirms enumeration and bidirectional data
 * transfer both work.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_device_test.h"

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>

static const struct device *cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static struct usbd_context *usbd;

K_SEM_DEFINE(dtr_sem, 0, 1);

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(ctx);
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
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

static void usb_device_test_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(cdc_dev)) {
		printk("USB1: CDC-ACM device not ready\n");
		return;
	}

	usbd = sample_usbd_init_device(usbd_msg_cb);
	if (usbd == NULL) {
		printk("USB1: failed to init USB device stack\n");
		return;
	}

	if (!usbd_can_detect_vbus(usbd)) {
		if (usbd_enable(usbd) != 0) {
			printk("USB1: failed to enable USB device support\n");
			return;
		}
	}

	printk("USB1 @ usbotg_hs1: USB device stack up - plug into a PC and open "
	       "the enumerated serial port\n");

	k_sem_take(&dtr_sem, K_FOREVER);
	printk("USB1: host opened the port - echoing input back\n");

	uart_irq_callback_set(cdc_dev, echo_isr);
	uart_irq_rx_enable(cdc_dev);
}

/* Delayed start so this thread's prints don't interleave with main()'s
 * boot-time scan/init output on the shared UART.
 */
K_THREAD_DEFINE(usb_device_test_tid, 2048, usb_device_test_thread, NULL, NULL, NULL, 7, 0, 1500);
