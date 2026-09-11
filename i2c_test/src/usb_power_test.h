/* SPDX-License-Identifier: Apache-2.0 */

#ifndef USB_POWER_TEST_H_
#define USB_POWER_TEST_H_

/* Enables both USB ports' MIC2026-1YM power switches (EN high) and
 * prints their initial OCS/fault status.
 */
void usb_power_test_init(void);

/* Polls both ports' OCS/fault status; only logs when it changes.
 * Call periodically (the app calls this once per 500ms tick).
 */
void usb_power_test_step(void);

#endif /* USB_POWER_TEST_H_ */
