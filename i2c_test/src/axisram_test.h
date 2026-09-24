/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AXISRAM_TEST_H_
#define AXISRAM_TEST_H_

#include <zephyr/shell/shell.h>

/* Writes and reads back a test pattern in a buffer placed in AXISRAM3. */
int axisram_test_run(const struct shell *sh);

#endif /* AXISRAM_TEST_H_ */
