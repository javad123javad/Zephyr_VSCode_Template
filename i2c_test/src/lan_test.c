/*
 * On-board Ethernet MAC (RMII, via mdio/eth_phy - generic MII PHY
 * driver) on mindos_n6. Starts a DHCPv4 client on every network
 * interface and prints the assigned IP/netmask/gateway once bound.
 *
 * No MAC address is programmed in OTP on virgin boards (see the
 * comment on &mac in mindos_n6_common.dtsi), so the driver falls
 * back to one derived from the chip's unique ID via HWINFO - stable
 * per board, but not a "real" assigned OUI.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lan_test.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>

static struct net_mgmt_event_callback mgmt_cb;

static void start_dhcpv4_client(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("LAN: starting DHCPv4 on %s (index=%d)\n",
	       net_if_get_device(iface)->name, net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

static void ipv4_addr_add_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				   struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char addr_buf[NET_IPV4_ADDR_LEN];
		char mask_buf[NET_IPV4_ADDR_LEN];
		char gw_buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}

		net_addr_ntop(NET_AF_INET, &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
			      addr_buf, sizeof(addr_buf));
		net_addr_ntop(NET_AF_INET, &iface->config.ip.ipv4->unicast[i].netmask, mask_buf,
			      sizeof(mask_buf));
		net_addr_ntop(NET_AF_INET, &iface->config.ip.ipv4->gw, gw_buf, sizeof(gw_buf));

		printk("LAN: DHCP bound - address=%s netmask=%s gateway=%s lease=%us\n", addr_buf,
		       mask_buf, gw_buf, iface->config.dhcpv4.lease_time);
	}
}

void lan_test_init(void)
{
	net_mgmt_init_event_callback(&mgmt_cb, ipv4_addr_add_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_if_foreach(start_dhcpv4_client, NULL);
}
