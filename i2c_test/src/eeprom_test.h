/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EEPROM_TEST_H_
#define EEPROM_TEST_H_

/* Checks the M24C64 board-configuration EEPROM (io_cnf_i2c @ 0x50):
 * dumps the start of its contents, then runs a write/read-back test on
 * the last page and restores that page's original contents afterwards.
 */
void eeprom_test_run(void);

#endif /* EEPROM_TEST_H_ */
