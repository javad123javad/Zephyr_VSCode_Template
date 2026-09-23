/*
 * Fake stand-in for wifi.c on native_sim: no real Wi-Fi/network stack
 * exists there, so this fabricates a ticking clock and a couple of scan
 * results, purely so the LVGL UI can be exercised without hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>

#include "wifi.h"

/* Arbitrary fixed reference so the simulated clock shows a plausible
 * date/time; it just ticks forward from here using uptime.
 */
#define SIM_BASE_EPOCH_S 1735689600LL /* 2025-01-01T00:00:00Z */
#define SIM_CONNECT_DELAY_MS 1000
#define SIM_SYNC_DELAY_MS 2000
#define SIM_SCAN_DELAY_MS 1500

static const struct wifi_scan_entry sim_networks[] = {
	{.ssid = "Simulated-Open", .ssid_len = 15, .secured = false},
	{.ssid = "Simulated-Secured", .ssid_len = 17, .secured = true},
};

static bool sim_saved[ARRAY_SIZE(sim_networks)];
static char sim_active_ssid[33] = "Simulated-WiFi";

static int64_t scan_start_uptime = -1;

enum wifi_net_status wifi_get_status(void)
{
	int64_t up = k_uptime_get();

	if (up < SIM_CONNECT_DELAY_MS) {
		return WIFI_STATUS_CONNECTING;
	}
	if (up < SIM_SYNC_DELAY_MS) {
		return WIFI_STATUS_CONNECTED;
	}
	return WIFI_STATUS_TIME_SYNCED;
}

const char *wifi_get_ssid(void)
{
	return sim_active_ssid;
}

int64_t wifi_current_epoch_s(void)
{
	if (k_uptime_get() < SIM_CONNECT_DELAY_MS) {
		return 0;
	}
	return SIM_BASE_EPOCH_S + k_uptime_get() / 1000;
}

void wifi_scan_start(void)
{
	scan_start_uptime = k_uptime_get();
}

bool wifi_scan_is_done(void)
{
	return scan_start_uptime >= 0 &&
	       (k_uptime_get() - scan_start_uptime) >= SIM_SCAN_DELAY_MS;
}

int wifi_scan_get_count(void)
{
	if (scan_start_uptime < 0) {
		return 0;
	}
	return wifi_scan_is_done() ? ARRAY_SIZE(sim_networks) : 0;
}

bool wifi_scan_get_entry(int index, struct wifi_scan_entry *out)
{
	if (index < 0 || index >= (int)ARRAY_SIZE(sim_networks)) {
		return false;
	}
	*out = sim_networks[index];
	return true;
}

void wifi_pause_reconnect(void)
{
}

void wifi_resume_reconnect(void)
{
}

static int sim_find(const char *ssid, size_t ssid_len)
{
	for (int i = 0; i < (int)ARRAY_SIZE(sim_networks); i++) {
		if (sim_networks[i].ssid_len == ssid_len &&
		    memcmp(sim_networks[i].ssid, ssid, ssid_len) == 0) {
			return i;
		}
	}
	return -1;
}

void wifi_connect_and_save(const char *ssid, size_t ssid_len, bool secured, const char *password,
			    size_t password_len)
{
	ARG_UNUSED(secured);
	ARG_UNUSED(password);
	ARG_UNUSED(password_len);

	int idx = sim_find(ssid, ssid_len);

	if (idx >= 0) {
		sim_saved[idx] = true;
	}

	size_t len = MIN(ssid_len, sizeof(sim_active_ssid) - 1);

	memcpy(sim_active_ssid, ssid, len);
	sim_active_ssid[len] = '\0';
}

bool wifi_is_saved(const char *ssid, size_t ssid_len)
{
	int idx = sim_find(ssid, ssid_len);

	return idx >= 0 && sim_saved[idx];
}

void wifi_forget(const char *ssid, size_t ssid_len)
{
	int idx = sim_find(ssid, ssid_len);

	if (idx >= 0) {
		sim_saved[idx] = false;
	}
}

void wifi_forget_all(void)
{
	memset(sim_saved, 0, sizeof(sim_saved));
}
