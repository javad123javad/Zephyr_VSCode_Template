/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_WIFI_H_
#define APP_WIFI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wifi_net_status {
	WIFI_STATUS_CONNECTING,
	WIFI_STATUS_CONNECTED,
	WIFI_STATUS_TIME_SYNCED,
};

struct wifi_scan_entry {
	char ssid[33];
	uint8_t ssid_len;
	bool secured;
};

enum wifi_net_status wifi_get_status(void);

/* SSID of the currently connected network, or "" if not connected. */
const char *wifi_get_ssid(void);

/* Current time as Unix epoch seconds, or 0 if never synced. */
int64_t wifi_current_epoch_s(void);

/* Starts an async scan; results arrive via wifi_scan_get_count()/_entry().
 * Disconnects first, since scanning while STA-connected can hang this
 * Wi-Fi driver; call wifi_pause_reconnect() before this (see below).
 */
void wifi_scan_start(void);
bool wifi_scan_is_done(void);
int wifi_scan_get_count(void);
bool wifi_scan_get_entry(int index, struct wifi_scan_entry *out);

/* Stops the background thread from auto-reconnecting, and disconnects.
 * Call before scanning or otherwise messing with the connection from the
 * Wi-Fi settings screen; call wifi_resume_reconnect() when leaving it.
 */
void wifi_pause_reconnect(void);
void wifi_resume_reconnect(void);

/* Saves credentials for ssid (persisted) and reconnects using them. */
void wifi_connect_and_save(const char *ssid, size_t ssid_len, bool secured,
			    const char *password, size_t password_len);

/* True if credentials for ssid are currently stored. */
bool wifi_is_saved(const char *ssid, size_t ssid_len);

/* Deletes stored credentials for ssid, if any. */
void wifi_forget(const char *ssid, size_t ssid_len);

/* Deletes every stored network's credentials. CONNECT_STORED tries every
 * saved network as a candidate on each attempt, so stale entries for
 * networks that are out of range at the current location (and therefore
 * cannot be individually forgotten from the scan list) can block the
 * intended one from ever being tried. This clears all of them.
 */
void wifi_forget_all(void);

#endif /* APP_WIFI_H_ */
