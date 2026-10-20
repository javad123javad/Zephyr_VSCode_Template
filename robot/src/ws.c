/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/websocket.h>

#include "drive.h"
#include "web.h"
#include "wifi.h"
#include "ws.h"

LOG_MODULE_REGISTER(ws, LOG_LEVEL_INF);

#define RECV_TIMEOUT_MS   100
#define SEND_TIMEOUT_MS   200
#define STATUS_PERIOD_MS  500
#define WS_STACK_SIZE     2048
#define WS_PRIORITY       K_PRIO_PREEMPT(8)

/* A connection waiting to replace the current one, or -1. */
static int pending_sock = -1;
static K_MUTEX_DEFINE(pending_lock);
static K_SEM_DEFINE(pending_sem, 0, 1);

/* Receive state of the current session, used by ws_thread only. */
static uint8_t rx_buf[32];
static size_t rx_len;
/* Set while skipping the rest of a message that did not fit. */
static bool rx_discard;

int ws_control_setup(int ws_socket, struct http_request_ctx *request_ctx, void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	(void)k_mutex_lock(&pending_lock, K_FOREVER);
	if (pending_sock >= 0) {
		(void)websocket_unregister(pending_sock);
	}
	pending_sock = ws_socket;
	(void)k_mutex_unlock(&pending_lock);

	k_sem_give(&pending_sem);

	return 0;
}

static int take_pending(void)
{
	int sock;

	(void)k_mutex_lock(&pending_lock, K_FOREVER);
	sock = pending_sock;
	pending_sock = -1;
	(void)k_mutex_unlock(&pending_lock);

	return sock;
}

static void close_session(int *sock)
{
	if (*sock >= 0) {
		(void)websocket_unregister(*sock);
		*sock = -1;
		rx_len = 0U;
		rx_discard = false;
		drive(DRIVE_STOP, 0U);
		LOG_INF("Control session closed");
	}
}

static int send_status(int sock)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "{\"rssi\":%d}", wifi_rssi());
	if ((len < 0) || ((size_t)len >= sizeof(buf))) {
		return -ENOMEM;
	}

	len = websocket_send_msg(sock, (const uint8_t *)buf, (size_t)len,
				 WEBSOCKET_OPCODE_DATA_TEXT, false, true, SEND_TIMEOUT_MS);

	return (len < 0) ? len : 0;
}

/* Returns 0 to keep the session, or a negative value to close it. */
static int receive(int sock)
{
	uint64_t remaining;
	uint32_t type;

	bool complete;
	int ret;

	ret = websocket_recv_msg(sock, &rx_buf[rx_len], sizeof(rx_buf) - 1U - rx_len, &type,
				 &remaining, RECV_TIMEOUT_MS);
	if (ret == -EAGAIN) {
		return 0;
	}
	if (ret < 0) {
		return ret;
	}

	if ((type & WEBSOCKET_FLAG_CLOSE) != 0U) {
		return -ENOTCONN;
	}

	if ((type & WEBSOCKET_FLAG_PING) != 0U) {
		ret = websocket_send_msg(sock, &rx_buf[rx_len], (size_t)ret,
					 WEBSOCKET_OPCODE_PONG, false, true, SEND_TIMEOUT_MS);
		return (ret < 0) ? ret : 0;
	}

	if ((type & WEBSOCKET_FLAG_TEXT) == 0U) {
		return 0;
	}

	complete = (remaining == 0U) && ((type & WEBSOCKET_FLAG_FINAL) != 0U);
	rx_len += (size_t)ret;

	/* Commands are a few bytes; skip any message that does not fit. */
	if (!complete && (rx_len >= (sizeof(rx_buf) - 1U))) {
		rx_discard = true;
	}

	if (rx_discard) {
		rx_len = 0U;
		rx_discard = !complete;
		return 0;
	}

	if (complete) {
		rx_buf[rx_len] = '\0';
		(void)web_command((char *)rx_buf);
		rx_len = 0U;
	}

	return 0;
}

static void ws_thread(void *p1, void *p2, void *p3)
{
	int64_t next_status = 0;
	int sock = -1;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		int next;

		if (sock < 0) {
			(void)k_sem_take(&pending_sem, K_FOREVER);
		}

		next = take_pending();
		if (next >= 0) {
			close_session(&sock);
			sock = next;
			next_status = 0;
			LOG_INF("Control session opened");
		}

		if (sock < 0) {
			continue;
		}

		if (receive(sock) < 0) {
			close_session(&sock);
			continue;
		}

		if (k_uptime_get() >= next_status) {
			next_status = k_uptime_get() + STATUS_PERIOD_MS;
			if (send_status(sock) < 0) {
				close_session(&sock);
			}
		}
	}
}

K_THREAD_DEFINE(ws_tid, WS_STACK_SIZE, ws_thread, NULL, NULL, NULL, WS_PRIORITY, 0, 0);
