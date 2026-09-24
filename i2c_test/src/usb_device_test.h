/* SPDX-License-Identifier: Apache-2.0 */

#ifndef USB_DEVICE_TEST_H_
#define USB_DEVICE_TEST_H_

#include <zephyr/shell/shell.h>

/* Brings up USB1 (usbotg_hs1) as a CDC-ACM serial device on first use
 * and waits up to 10 s for a host to enumerate it. Afterwards the port
 * echoes everything typed into it.
 */
int usb_device_test_run(const struct shell *sh);

#endif /* USB_DEVICE_TEST_H_ */
