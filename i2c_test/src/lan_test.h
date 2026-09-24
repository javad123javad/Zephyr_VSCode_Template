/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LAN_TEST_H_
#define LAN_TEST_H_

/* Starts the DHCPv4 client on every network interface (just the
 * on-board Ethernet MAC here) and registers a callback that prints
 * the assigned IP/netmask/gateway once DHCP completes. Non-blocking -
 * DHCP negotiation happens asynchronously in the net stack's own
 * thread, so this returns immediately regardless of link state.
 */
void lan_test_init(void);

#endif /* LAN_TEST_H_ */
