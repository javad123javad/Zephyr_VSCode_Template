/* SPDX-License-Identifier: Apache-2.0 */

#ifndef I2C_SCAN_H_
#define I2C_SCAN_H_

#include <zephyr/shell/shell.h>

/* Scans both on-board I2C buses (io_cnf_i2c / io_i2c) for addresses
 * 0x04-0x77, prints a scan table and fails if a device described in the
 * overlays does not answer.
 */
int i2c_scan_test_run(const struct shell *sh);

#endif /* I2C_SCAN_H_ */
