/* SPDX-License-Identifier: Apache-2.0 */

#ifndef USB_POWER_TEST_H_
#define USB_POWER_TEST_H_

#include <zephyr/shell/shell.h>

/* Configures both MIC2026-1YM USB power switches: 5V on for USB2, held
 * off for USB1. Called at boot so USB2 power is always available.
 */
void usb_power_init(void);

/* Reports each port's switch state and fails on an OCS/fault flag. */
int usb_power_test_run(const struct shell *sh);

#endif /* USB_POWER_TEST_H_ */
