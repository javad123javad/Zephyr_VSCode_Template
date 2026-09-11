/* SPDX-License-Identifier: Apache-2.0 */

#ifndef I2C_SCAN_H_
#define I2C_SCAN_H_

/* Scans both on-board I2C buses (io_cnf_i2c / io_i2c) for addresses
 * 0x04-0x77 and prints a scan table (same format as the "i2c scan"
 * shell command) showing any address that ACKs.
 */
void i2c_scan_all(void);

#endif /* I2C_SCAN_H_ */
