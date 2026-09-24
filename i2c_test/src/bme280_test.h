/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BME280_TEST_H_
#define BME280_TEST_H_

#include <zephyr/shell/shell.h>

/* Takes one BME280 (io_i2c @ 0x76) sample, prints it and checks it is
 * within the sensor's operating range.
 */
int bme280_test_run(const struct shell *sh);

#endif /* BME280_TEST_H_ */
