/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/sntp.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

#include "wifi.h"

#define NTP_SERVER "pool.ntp.org"
#define NTP_RESYNC_PERIOD K_HOURS(1)
#define MAX_SCAN_RESULTS 20

static atomic_t net_status = ATOMIC_INIT(WIFI_STATUS_CONNECTING);
static atomic_t reconnect_paused = ATOMIC_INIT(0);

static struct k_mutex time_lock;
static int64_t base_epoch_s;
static int64_t base_uptime_ms;

static struct k_mutex ssid_lock;
static char current_ssid[33];

static struct k_mutex scan_lock;
static struct wifi_scan_entry scan_results[MAX_SCAN_RESULTS];
static int scan_count;
static bool scan_done = true;

static void set_synced_time(uint64_t epoch_s)
{
	k_mutex_lock(&time_lock, K_FOREVER);
	base_epoch_s = (int64_t)epoch_s;
	base_uptime_ms = k_uptime_get();
	k_mutex_unlock(&time_lock);
}

int64_t wifi_current_epoch_s(void)
{
	int64_t epoch, uptime;

	k_mutex_lock(&time_lock, K_FOREVER);
	epoch = base_epoch_s;
	uptime = base_uptime_ms;
	k_mutex_unlock(&time_lock);

	if (epoch == 0) {
		return 0;
	}

	return epoch + (k_uptime_get() - uptime) / MSEC_PER_SEC;
}

enum wifi_net_status wifi_get_status(void)
{
	return (enum wifi_net_status)atomic_get(&net_status);
}

const char *wifi_get_ssid(void)
{
	return current_ssid;
}

static void update_current_ssid(struct net_if *iface)
{
	struct wifi_iface_status status = {0};

	k_mutex_lock(&ssid_lock, K_FOREVER);
	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status)) == 0) {
		size_t len = MIN(status.ssid_len, sizeof(current_ssid) - 1);

		memcpy(current_ssid, status.ssid, len);
		current_ssid[len] = '\0';
	}
	k_mutex_unlock(&ssid_lock);
}

void wifi_pause_reconnect(void)
{
	atomic_set(&reconnect_paused, 1);
	net_mgmt(NET_REQUEST_WIFI_DISCONNECT, net_if_get_wifi_sta(), NULL, 0);
}

void wifi_resume_reconnect(void)
{
	atomic_set(&reconnect_paused, 0);
}

void wifi_scan_start(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct wifi_scan_params params = {0};

	k_mutex_lock(&scan_lock, K_FOREVER);
	scan_count = 0;
	scan_done = false;
	k_mutex_unlock(&scan_lock);

	int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(params));

	LOG_INF("Scan requested: %d", ret);
	if (ret) {
		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_done = true;
		k_mutex_unlock(&scan_lock);
	}
}

bool wifi_scan_is_done(void)
{
	bool done;

	k_mutex_lock(&scan_lock, K_FOREVER);
	done = scan_done;
	k_mutex_unlock(&scan_lock);

	return done;
}

int wifi_scan_get_count(void)
{
	int count;

	k_mutex_lock(&scan_lock, K_FOREVER);
	count = scan_count;
	k_mutex_unlock(&scan_lock);

	return count;
}

bool wifi_scan_get_entry(int index, struct wifi_scan_entry *out)
{
	bool ok = false;

	k_mutex_lock(&scan_lock, K_FOREVER);
	if (index >= 0 && index < scan_count) {
		*out = scan_results[index];
		ok = true;
	}
	k_mutex_unlock(&scan_lock);

	return ok;
}

void wifi_connect_and_save(const char *ssid, size_t ssid_len, bool secured,
			    const char *password, size_t password_len)
{
	enum wifi_security_type type = secured ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	int ret = wifi_credentials_set_personal(ssid, ssid_len, type, NULL, 0,
						 secured ? password : NULL,
						 secured ? password_len : 0, 0, 0, 0);

	if (ret != 0) {
		LOG_ERR("Failed to save credentials for \"%.*s\": %d", (int)ssid_len, ssid, ret);
		return;
	}

	net_mgmt(NET_REQUEST_WIFI_DISCONNECT, net_if_get_wifi_sta(), NULL, 0);
	wifi_resume_reconnect();
}

bool wifi_is_saved(const char *ssid, size_t ssid_len)
{
	struct wifi_credentials_personal creds;

	return wifi_credentials_get_by_ssid_personal_struct(ssid, ssid_len, &creds) == 0;
}

void wifi_forget(const char *ssid, size_t ssid_len)
{
	int ret = wifi_credentials_delete_by_ssid(ssid, ssid_len);

	if (ret != 0) {
		LOG_ERR("Failed to forget \"%.*s\": %d", (int)ssid_len, ssid, ret);
	}
}

void wifi_forget_all(void)
{
	int ret = wifi_credentials_delete_all();

	if (ret != 0) {
		LOG_ERR("Failed to forget all networks: %d", ret);
	}
}

static void handle_scan_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_scan_result *entry = (const struct wifi_scan_result *)cb->info;
	size_t len = MIN(entry->ssid_length, sizeof(scan_results[0].ssid) - 1);

	LOG_INF("Scan result: %.*s (security=%d)", (int)len, entry->ssid, entry->security);

	k_mutex_lock(&scan_lock, K_FOREVER);
	for (int i = 0; i < scan_count; i++) {
		if (scan_results[i].ssid_len == len &&
		    memcmp(scan_results[i].ssid, entry->ssid, len) == 0) {
			k_mutex_unlock(&scan_lock);
			return;
		}
	}
	if (scan_count < MAX_SCAN_RESULTS) {
		memcpy(scan_results[scan_count].ssid, entry->ssid, len);
		scan_results[scan_count].ssid[len] = '\0';
		scan_results[scan_count].ssid_len = len;
		scan_results[scan_count].secured = entry->security != WIFI_SECURITY_TYPE_NONE;
		scan_count++;
	}
	k_mutex_unlock(&scan_lock);
}

#define WIFI_SCAN_EVENT_MASK (NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback l4_cb;
static struct k_sem network_connected;
static struct k_sem network_disconnected;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
				struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		handle_scan_result(cb);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		LOG_INF("Scan done, %d result(s)", scan_count);
		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_done = true;
		k_mutex_unlock(&scan_lock);
		break;
	default:
		break;
	}
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			      struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	switch (event) {
	case NET_EVENT_L4_CONNECTED:
		k_sem_give(&network_connected);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		atomic_set(&net_status, WIFI_STATUS_CONNECTING);
		k_sem_give(&network_disconnected);
		break;
	default:
		break;
	}
}

static void network_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct net_if *iface = net_if_get_wifi_sta();

	k_mutex_init(&time_lock);
	k_mutex_init(&ssid_lock);
	k_mutex_init(&scan_lock);
	k_sem_init(&network_connected, 0, 1);
	k_sem_init(&network_disconnected, 0, 1);

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_SCAN_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);

	net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&l4_cb);
	conn_mgr_mon_resend_status();

	while (1) {
		while (atomic_get(&reconnect_paused)) {
			k_sleep(K_MSEC(200));
		}

		net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

		/* Bounded, not K_FOREVER: a connect attempt that fails before
		 * ever associating (bad credentials, AP not found, ...) never
		 * raises NET_EVENT_L4_CONNECTED, so an unbounded wait here
		 * would hang forever and never notice credentials saved later
		 * from the Wi-Fi setup screen. Retrying periodically means a
		 * fresh wifi_connect_and_save() gets picked up on the next
		 * NET_REQUEST_WIFI_CONNECT_STORED lookup.
		 */
		if (k_sem_take(&network_connected, K_SECONDS(10)) != 0) {
			continue;
		}
		k_sem_reset(&network_disconnected);
		update_current_ssid(iface);
		atomic_set(&net_status, WIFI_STATUS_CONNECTED);
		LOG_INF("Wi-Fi connected: %s", current_ssid);

		do {
			struct sntp_time ts;
			int ret = sntp_simple(NTP_SERVER, 5000, &ts);

			if (ret == 0) {
				set_synced_time(ts.seconds);
				atomic_set(&net_status, WIFI_STATUS_TIME_SYNCED);
				LOG_INF("Time synced: %llu", ts.seconds);
			} else {
				LOG_WRN("SNTP query failed: %d", ret);
			}
		} while (k_sem_take(&network_disconnected, NTP_RESYNC_PERIOD) != 0);

		LOG_WRN("Wi-Fi disconnected, reconnecting");
	}
}

K_THREAD_DEFINE(network_tid, 4096, network_thread, NULL, NULL, NULL, 5, 0, 0);
