/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "drive.h"
#include "wifi.h"

LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define IPV4_EVENTS NET_EVENT_IPV4_ADDR_ADD

#define RECONNECT_DELAY K_SECONDS(5)

static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static void connect_work_handler(struct k_work *work)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)CONFIG_ROBOT_WIFI_SSID,
		.ssid_length = (uint8_t)strlen(CONFIG_ROBOT_WIFI_SSID),
		.psk = (const uint8_t *)CONFIG_ROBOT_WIFI_PSK,
		.psk_length = (uint8_t)strlen(CONFIG_ROBOT_WIFI_PSK),
		.security = (strlen(CONFIG_ROBOT_WIFI_PSK) > 0U) ? WIFI_SECURITY_TYPE_PSK
								  : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.timeout = SYS_FOREVER_MS,
	};
	int err;

	ARG_UNUSED(work);

	LOG_INF("Connecting to \"%s\"", CONFIG_ROBOT_WIFI_SSID);

	err = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &params, sizeof(params));
	if (err != 0) {
		LOG_ERR("Connect request failed (%d)", err);
	}
}

static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_handler);

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->status != 0) {
			LOG_WRN("Connection failed (%d), retrying", status->status);
			(void)k_work_reschedule(&connect_work, RECONNECT_DELAY);
		} else {
			LOG_INF("Connected, waiting for an address");
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_WRN("Disconnected, retrying");
		drive(DRIVE_STOP, 0U);
		(void)k_work_reschedule(&connect_work, RECONNECT_DELAY);
		break;
	default:
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];
	struct net_in_addr *addr;

	ARG_UNUSED(cb);

	if ((mgmt_event != NET_EVENT_IPV4_ADDR_ADD) || (iface != sta_iface)) {
		return;
	}

	addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (addr == NULL) {
		return;
	}

	if (net_addr_ntop(NET_AF_INET, addr, buf, sizeof(buf)) != NULL) {
		LOG_INF("Open http://%s.local/ or http://%s/ to drive the robot",
			CONFIG_NET_HOSTNAME, buf);
	}
}

int wifi_start(void)
{
	if (strlen(CONFIG_ROBOT_WIFI_SSID) == 0U) {
		LOG_ERR("CONFIG_ROBOT_WIFI_SSID is not set");
		return -EINVAL;
	}

	sta_iface = net_if_get_wifi_sta();
	if (sta_iface == NULL) {
		LOG_ERR("No Wi-Fi station interface");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, IPV4_EVENTS);
	net_mgmt_add_event_callback(&ipv4_cb);

	net_dhcpv4_start(sta_iface);

	(void)k_work_reschedule(&connect_work, K_NO_WAIT);

	return 0;
}
