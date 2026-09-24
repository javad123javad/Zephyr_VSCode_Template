/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PCA9557_TEST_H_
#define PCA9557_TEST_H_

#include <zephyr/shell/shell.h>

/* Walks a high bit across all 8 pins of the PCA9557PW,118 GPIO expander
 * (io_i2c @ 0x19), verifying every step, then drives all pins low.
 */
int pca9557_test_run(const struct shell *sh);

#endif /* PCA9557_TEST_H_ */
