/* SPDX-License-Identifier: Apache-2.0 */

#ifndef CAN_TEST_H_
#define CAN_TEST_H_

/* Starts the MCP2515 CAN controller (io_spi, 8MHz osc, INT on PB3,
 * CS via spi2's hardware NSS). Defaults to internal loopback mode
 * (see CAN_TEST_LOOPBACK in can_test.c) so the controller and SPI
 * link are exercised without needing a second node on the bus.
 */
void can_test_init(void);

/* Sends a test frame every CAN_TEST_PERIOD_TICKS ticks and prints
 * anything received. Call once per 500ms tick.
 */
void can_test_step(unsigned int tick);

#endif /* CAN_TEST_H_ */
