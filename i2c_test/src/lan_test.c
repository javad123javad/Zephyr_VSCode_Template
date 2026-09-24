/*
 * On-board Ethernet MAC (RMII, via mdio/eth_phy - generic MII PHY
 * driver) on mindos_n6. lan_start() runs the DHCPv4 client from boot,
 * so networking (e.g. "net ping") works without running the test; the
 * test checks the interface, link and DHCP lease.
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
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>

#define LAN_TEST_TIMEOUT_MS 15000
#define LAN_TEST_POLL_MS    100

static struct net_mgmt_event_callback mgmt_cb;

static void start_dhcpv4_client(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("LAN: starting DHCPv4 on %s (index=%d)\n",
	       net_if_get_device(iface)->name, net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

/* Returns the index of the DHCP-assigned unicast address, or -1 */
static int find_dhcp_addr(struct net_if *iface)
{
	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

	if (ipv4 == NULL) {
		return -1;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.is_used &&
		    (ipv4->unicast[i].ipv4.addr_type == NET_ADDR_DHCP)) {
			return i;
		}
	}

	return -1;
}

static void print_lease(const struct shell *sh, struct net_if *iface, int idx)
{
	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	char addr_buf[NET_IPV4_ADDR_LEN];
	char mask_buf[NET_IPV4_ADDR_LEN];
	char gw_buf[NET_IPV4_ADDR_LEN];

	net_addr_ntop(NET_AF_INET, &ipv4->unicast[idx].ipv4.address.in_addr, addr_buf,
		      sizeof(addr_buf));
	net_addr_ntop(NET_AF_INET, &ipv4->unicast[idx].netmask, mask_buf, sizeof(mask_buf));
	net_addr_ntop(NET_AF_INET, &ipv4->gw, gw_buf, sizeof(gw_buf));

	if (sh != NULL) {
		shell_print(sh, "LAN: DHCP bound - address=%s netmask=%s gateway=%s lease=%us",
			    addr_buf, mask_buf, gw_buf, iface->config.dhcpv4.lease_time);
	} else {
		printk("LAN: DHCP bound - address=%s netmask=%s gateway=%s lease=%us\n",
		       addr_buf, mask_buf, gw_buf, iface->config.dhcpv4.lease_time);
	}
}

static void ipv4_addr_add_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				   struct net_if *iface)
{
	int idx;

	ARG_UNUSED(cb);

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	idx = find_dhcp_addr(iface);
	if (idx >= 0) {
		print_lease(NULL, iface, idx);
	}
}

void lan_start(void)
{
	net_mgmt_init_event_callback(&mgmt_cb, ipv4_addr_add_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_if_foreach(start_dhcpv4_client, NULL);
}

int lan_test_run(const struct shell *sh)
{
	struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
	struct net_linkaddr *mac;
	int idx = -1;

	if (iface == NULL) {
		shell_error(sh, "LAN: no Ethernet interface (is CONFIG_ETH_DRIVER enabled?)");
		return -ENODEV;
	}

	mac = net_if_get_link_addr(iface);
	shell_print(sh, "LAN: %s (index=%d), MAC %02x:%02x:%02x:%02x:%02x:%02x",
		    net_if_get_device(iface)->name, net_if_get_by_iface(iface),
		    mac->addr[0], mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4],
		    mac->addr[5]);

	if (iface->config.dhcpv4.state == NET_DHCPV4_DISABLED) {
		net_dhcpv4_start(iface);
	}

	for (int waited = 0; waited < LAN_TEST_TIMEOUT_MS; waited += LAN_TEST_POLL_MS) {
		idx = find_dhcp_addr(iface);
		if (idx >= 0) {
			break;
		}
		k_msleep(LAN_TEST_POLL_MS);
	}

	if (idx < 0) {
		shell_error(sh, "LAN: no DHCP lease within %d s (link %s, DHCP state %s)",
			    LAN_TEST_TIMEOUT_MS / 1000,
			    net_if_is_carrier_ok(iface) ? "up" : "DOWN - cable?",
			    net_dhcpv4_state_name(iface->config.dhcpv4.state));
		return -ETIMEDOUT;
	}

	print_lease(sh, iface, idx);
	return 0;
}
