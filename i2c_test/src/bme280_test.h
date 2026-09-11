/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BME280_TEST_H_
#define BME280_TEST_H_

/* Checks the BME280 (io_i2c @ 0x76) is ready and prints a status line. */
void bme280_test_init(void);

/* Fetches one sample and prints temperature/pressure/humidity. Call
 * periodically (the app calls this every 4th 500ms tick, i.e. every 2s).
 */
void bme280_test_sample(void);

#endif /* BME280_TEST_H_ */
