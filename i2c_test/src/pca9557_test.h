/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PCA9557_TEST_H_
#define PCA9557_TEST_H_

/* Configures all 8 pins of the PCA9557PW,118 GPIO expander (io_i2c
 * @ 0x19) as outputs, ready for pca9557_test_step().
 */
void pca9557_test_init(void);

/* Advances a walking-bit pattern by one pin. Call periodically (the
 * app calls this once per 500ms tick). Errors only are logged - no
 * per-step logging, to keep the console usable.
 */
void pca9557_test_step(void);

#endif /* PCA9557_TEST_H_ */
