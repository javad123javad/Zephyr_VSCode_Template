/* SPDX-License-Identifier: Apache-2.0 */

#ifndef I2S_TEST_H_
#define I2S_TEST_H_

#include <zephyr/shell/shell.h>

/* Plays a ~689 Hz test tone into the MAX98357A on io_i2s for 2 s and
 * fails on any driver error or underrun. Whether the tone was actually
 * audible has to be checked by ear.
 */
int i2s_test_run(const struct shell *sh);

#endif /* I2S_TEST_H_ */
