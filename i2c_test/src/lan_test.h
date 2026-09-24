/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LAN_TEST_H_
#define LAN_TEST_H_

#include <zephyr/shell/shell.h>

/* Starts the DHCPv4 client on every network interface (just the
 * on-board Ethernet MAC here) and prints the lease once bound.
 * Non-blocking; called at boot so networking is available without
 * running the test.
 */
void lan_start(void);

/* Checks that the Ethernet interface exists and obtains a DHCPv4
 * lease, waiting up to 15 s for it.
 */
int lan_test_run(const struct shell *sh);

#endif /* LAN_TEST_H_ */
