/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "drive.h"
#include "ota.h"
#include "wifi.h"

LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

#define WIFI_EVENTS                                                                                \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_STA_CONNECTED |                       \
	 NET_EVENT_WIFI_AP_STA_DISCONNECTED)
#define IPV4_EVENTS NET_EVENT_IPV4_ADDR_ADD

/* Station retry period, and the longer one while the own AP is up: every
 * station scan moves the shared radio channel and briefly drops AP clients.
 */
#define STA_RETRY       K_SECONDS(5)
#define STA_RETRY_AP    K_SECONDS(60)
#define AP_FALLBACK     K_SECONDS(CONFIG_ROBOT_AP_FALLBACK_SEC)

#define AP_ADDR         "192.168.4.1"
#define AP_NETMASK      "255.255.255.0"
#define AP_POOL_START   "192.168.4.10"

static struct net_if *sta_iface;
static struct net_if *ap_iface;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

/* Protected by wifi_lock. */
static K_MUTEX_DEFINE(wifi_lock);
static bool sta_connected;
static bool ap_active;
static uint32_t ap_clients;

static bool sta_configured(void)
{
	return strlen(CONFIG_ROBOT_WIFI_SSID) > 0U;
}

static bool ap_configured(void)
{
	size_t len = strlen(CONFIG_ROBOT_AP_PSK);

	return (ap_iface != NULL) && (len >= 8U) && (len <= 63U);
}

static void connect_work_handler(struct k_work *work);
static void ap_on_work_handler(struct k_work *work);
static void ap_off_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_handler);
static K_WORK_DELAYABLE_DEFINE(ap_on_work, ap_on_work_handler);
static K_WORK_DELAYABLE_DEFINE(ap_off_work, ap_off_work_handler);

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
	bool busy_ap;
	int err;

	ARG_UNUSED(work);

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	busy_ap = ap_active && (ap_clients > 0U);
	(void)k_mutex_unlock(&wifi_lock);

	/* Someone drives through the own AP: do not disturb the channel. */
	if (busy_ap) {
		(void)k_work_reschedule(&connect_work, STA_RETRY_AP);
		return;
	}

	LOG_INF("Connecting to \"%s\"", CONFIG_ROBOT_WIFI_SSID);

	err = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &params, sizeof(params));
	if (err != 0) {
		LOG_ERR("Connect request failed (%d)", err);
		(void)k_work_reschedule(&connect_work, ap_active ? STA_RETRY_AP : STA_RETRY);
	}
}

/* Give the AP interface its fixed address once; it survives AP restarts. */
static int ap_setup_address(void)
{
	static bool done;
	struct net_in_addr addr;
	struct net_in_addr netmask;

	if (done) {
		return 0;
	}

	if ((net_addr_pton(NET_AF_INET, AP_ADDR, &addr) != 0) ||
	    (net_addr_pton(NET_AF_INET, AP_NETMASK, &netmask) != 0)) {
		return -EINVAL;
	}

	if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		return -ENOMEM;
	}

	if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask)) {
		return -EINVAL;
	}

	net_if_ipv4_set_gw(ap_iface, &addr);
	done = true;

	return 0;
}

static void ap_on_work_handler(struct k_work *work)
{
	struct wifi_connect_req_params params = {
		.psk = (const uint8_t *)CONFIG_ROBOT_AP_PSK,
		.psk_length = (uint8_t)strlen(CONFIG_ROBOT_AP_PSK),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};
	static char ssid[WIFI_SSID_MAX_LEN + 1];
	const struct net_linkaddr *mac = net_if_get_link_addr(ap_iface);
	struct net_in_addr pool;
	bool skip;
	int err;

	ARG_UNUSED(work);

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	skip = sta_connected || ap_active;
	(void)k_mutex_unlock(&wifi_lock);

	if (skip) {
		return;
	}

	err = ap_setup_address();
	if (err == 0) {
		(void)net_addr_pton(NET_AF_INET, AP_POOL_START, &pool);
		err = net_dhcpv4_server_start(ap_iface, &pool);
		if (err == -EALREADY) {
			err = 0;
		}
	}
	if (err != 0) {
		LOG_ERR("AP network setup failed (%d)", err);
		return;
	}

	(void)snprintf(ssid, sizeof(ssid), "%s-%02x%02x", CONFIG_NET_HOSTNAME,
		       mac->addr[mac->len - 2U], mac->addr[mac->len - 1U]);
	params.ssid = (const uint8_t *)ssid;
	params.ssid_length = (uint8_t)strlen(ssid);

	err = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &params, sizeof(params));
	if (err != 0) {
		LOG_ERR("AP enable failed (%d)", err);
		(void)net_dhcpv4_server_stop(ap_iface);
		return;
	}

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	ap_active = true;
	(void)k_mutex_unlock(&wifi_lock);

	LOG_INF("Own network \"%s\" is up: join it and open http://" AP_ADDR "/", ssid);
	ota_confirm();
}

static void ap_off_work_handler(struct k_work *work)
{
	bool off;

	ARG_UNUSED(work);

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	off = sta_connected && ap_active && (ap_clients == 0U);
	if (off) {
		ap_active = false;
	}
	(void)k_mutex_unlock(&wifi_lock);

	if (!off) {
		return;
	}

	(void)net_dhcpv4_server_stop(ap_iface);
	if (net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0) != 0) {
		LOG_WRN("AP disable failed");
	} else {
		LOG_INF("Own network down, using \"%s\"", CONFIG_ROBOT_WIFI_SSID);
	}
}

static void sta_event(uint64_t mgmt_event, const struct wifi_status *status)
{
	bool retry = false;

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->status != 0) {
			LOG_WRN("Connection failed (%d), retrying", status->status);
			retry = true;
		} else {
			LOG_INF("Connected, waiting for an address");
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_WRN("Disconnected, retrying");
		(void)k_mutex_lock(&wifi_lock, K_FOREVER);
		sta_connected = false;
		(void)k_mutex_unlock(&wifi_lock);
		drive(DRIVE_STOP, 0U);
		retry = true;
		if (ap_configured()) {
			(void)k_work_reschedule(&ap_on_work, AP_FALLBACK);
		}
		break;
	default:
		break;
	}

	if (retry) {
		(void)k_work_reschedule(&connect_work, ap_active ? STA_RETRY_AP : STA_RETRY);
	}
}

static void ap_event(uint64_t mgmt_event)
{
	uint32_t clients;

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	if (mgmt_event == NET_EVENT_WIFI_AP_STA_CONNECTED) {
		ap_clients++;
	} else if ((mgmt_event == NET_EVENT_WIFI_AP_STA_DISCONNECTED) && (ap_clients > 0U)) {
		ap_clients--;
	}
	clients = ap_clients;
	(void)k_mutex_unlock(&wifi_lock);

	if (mgmt_event == NET_EVENT_WIFI_AP_ENABLE_RESULT) {
		return;
	}

	LOG_INF("%u device(s) on the own network", (unsigned int)clients);

	/* The last client left: switch back to the station if it is up. */
	if (clients == 0U) {
		(void)k_work_reschedule(&ap_off_work, K_NO_WAIT);
	}
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	if ((iface == ap_iface) && (ap_iface != NULL)) {
		ap_event(mgmt_event);
	} else if (iface == sta_iface) {
		sta_event(mgmt_event, (const struct wifi_status *)cb->info);
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

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	sta_connected = true;
	(void)k_mutex_unlock(&wifi_lock);

	(void)k_work_cancel_delayable(&ap_on_work);
	(void)k_work_reschedule(&ap_off_work, K_NO_WAIT);
	ota_confirm();

	if (net_addr_ntop(NET_AF_INET, addr, buf, sizeof(buf)) != NULL) {
		LOG_INF("Open http://%s.local/ or http://%s/ to drive the robot",
			CONFIG_NET_HOSTNAME, buf);
	}
}

int wifi_rssi(void)
{
	struct wifi_iface_status status = {0};

	if ((sta_iface == NULL) ||
	    (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta_iface, &status, sizeof(status)) != 0) ||
	    (status.state < WIFI_STATE_ASSOCIATED)) {
		return 0;
	}

	return status.rssi;
}

int wifi_start(void)
{
	sta_iface = net_if_get_wifi_sta();
	ap_iface = net_if_get_wifi_sap();

	if (sta_iface == NULL) {
		LOG_ERR("No Wi-Fi station interface");
		return -ENODEV;
	}

	if (!ap_configured()) {
		LOG_WRN("Own network disabled: set CONFIG_ROBOT_AP_PSK (8-63 characters)");
	}

	if (!sta_configured() && !ap_configured()) {
		LOG_ERR("Neither CONFIG_ROBOT_WIFI_SSID nor CONFIG_ROBOT_AP_PSK is set");
		return -EINVAL;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, IPV4_EVENTS);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (ap_configured()) {
		(void)k_work_reschedule(&ap_on_work, sta_configured() ? AP_FALLBACK : K_NO_WAIT);
	}

	if (sta_configured()) {
		net_dhcpv4_start(sta_iface);
		(void)k_work_reschedule(&connect_work, K_NO_WAIT);
	}

	return 0;
}
