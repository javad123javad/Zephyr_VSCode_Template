/* SPDX-License-Identifier: Apache-2.0 */

#ifndef USB_DEVICE_TEST_H_
#define USB_DEVICE_TEST_H_

/* No API: this module runs itself. Linking usb_device_test.c in is
 * enough - it starts its own thread that brings up the USB device
 * stack on USB1 (usbotg_hs1) as a CDC-ACM serial port. See
 * usb_device_test.c for the important VBUS-conflict warning before
 * plugging that port into a PC.
 */

#endif /* USB_DEVICE_TEST_H_ */
