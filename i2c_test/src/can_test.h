/* SPDX-License-Identifier: Apache-2.0 */

#ifndef CAN_TEST_H_
#define CAN_TEST_H_

#include <zephyr/shell/shell.h>

/* Starts the MCP2515 CAN controller (card SPI port 0) on first use, sends one
 * frame in loopback mode and checks it is received back.
 */
int can_test_run(const struct shell *sh);

#endif /* CAN_TEST_H_ */
