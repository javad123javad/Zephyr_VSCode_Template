/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "battery.h"
#include "drive.h"
#include "health.h"
#include "imu.h"
#include "link.h"
#include "wifi.h"

LOG_MODULE_REGISTER(link, LOG_LEVEL_INF);

#define LINK_RX_STACK_SIZE 1536
#define LINK_TX_STACK_SIZE 1536
#define LINK_RX_PRIORITY   K_PRIO_PREEMPT(5)
#define LINK_TX_PRIORITY   K_PRIO_PREEMPT(10)
#define POLL_MS           10
#define STATUS_PERIOD_MS  500

/*
 * A drive command runs this long. The PC repeats it while a key is held, so
 * this only bounds how long the robot keeps moving after the link drops.
 */
#define LINK_DRIVE_MS     700U

#define BATCH_SAMPLES     20
#define BATCH_MAX_AGE_MS  50

static uint8_t pkt[sizeof(struct link_imu_header) + (BATCH_SAMPLES * sizeof(struct imu_sample))];

/* Session state, shared by the receive and the send thread. */
static K_MUTEX_DEFINE(session_lock);
static int sock = -1;
static bool session;
static struct net_sockaddr_in peer;
static int64_t expires;
static int64_t last_drive;
static uint32_t drive_cmds;
static uint32_t drive_gap_max_ms;
static uint32_t send_max_ms;
static K_SEM_DEFINE(sock_ready, 0, 1);

static int handle_command(char *cmd)
{
	long val[3];
	char *pos = &cmd[1];

	switch (cmd[0]) {
	case 'S':
	case 'X':
		return 0;
	case 'R':
		drive_inhibit_clear(DRIVE_INHIBIT_LATCHED);
		return 0;
	case 'D':
		break;
	default:
		return -EINVAL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(val); i++) {
		char *end;

		val[i] = strtol(pos, &end, 10);
		if (end == pos) {
			return -EINVAL;
		}
		pos = end;
	}

	drive_mix((int32_t)val[0], (int32_t)val[1], (int32_t)val[2], LINK_DRIVE_MS);

	return 0;
}

static int32_t battery_mv(enum battery_id id, uint8_t *low)
{
	struct battery_state st;

	if (battery_get(id, &st) != 0) {
		return -1;
	}

	if (st.low || st.empty) {
		*low |= (uint8_t)BIT(id);
	}

	return (int32_t)st.mv;
}

static void send_status(int s, const struct net_sockaddr_in *to)
{
	struct link_status st = {
		.magic = LINK_STATUS_MAGIC,
		.version = {APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL},
		.rssi = (int8_t)wifi_rssi(),
		.inhibit = (uint8_t)drive_inhibit_get(),
		.uptime_ms = (uint32_t)k_uptime_get(),
	};
	struct imu_stats imu;

	st.motor_mv = battery_mv(BATTERY_MOTOR, &st.battery_low);
	st.board_mv = battery_mv(BATTERY_BOARD, &st.battery_low);

	imu_get(&imu, NULL);
	st.imu_ok = imu.accel_ok ? 1U : 0U;
	st.imu_samples = imu.samples;
	st.imu_int_timeouts = imu.int_timeouts;
	st.imu_dropped = imu.dropped;
	st.gyro_bias_updates = imu.bias_updates;
	for (size_t i = 0U; i < 3U; i++) {
		st.gyro_bias_mdps[i] =
			(imu.gyro_bias_q4[i] * 1000000) / (16 * IMU_GYRO_LSB_PER_DPS_X1000);
	}

	(void)k_mutex_lock(&session_lock, K_FOREVER);
	st.drive_cmds = drive_cmds;
	st.drive_gap_max_ms = drive_gap_max_ms;
	st.send_max_ms = send_max_ms;
	(void)k_mutex_unlock(&session_lock);

	(void)zsock_sendto(s, &st, sizeof(st), 0, (const struct net_sockaddr *)to, sizeof(*to));
}

static void send_imu(int s, const struct net_sockaddr_in *to, uint32_t seq)
{
	struct link_imu_header *hdr = (struct link_imu_header *)pkt;
	struct imu_sample *samples = (struct imu_sample *)&pkt[sizeof(*hdr)];
	struct imu_stats imu;
	size_t n = imu_stream_take(samples, BATCH_SAMPLES);

	imu_get(&imu, NULL);

	hdr->magic = LINK_IMU_MAGIC;
	hdr->seq = seq;
	hdr->dropped = imu.dropped;
	hdr->int_timeouts = imu.int_timeouts;
	hdr->rate_hz = CONFIG_ROBOT_IMU_RATE;
	hdr->count = (uint8_t)n;
	hdr->sample_size = sizeof(struct imu_sample);

	(void)zsock_sendto(s, pkt, sizeof(*hdr) + (n * sizeof(struct imu_sample)), 0,
			   (const struct net_sockaddr *)to, sizeof(*to));
}


static bool session_peer(struct net_sockaddr_in *out)
{
	bool on;

	(void)k_mutex_lock(&session_lock, K_FOREVER);
	on = session;
	if (on) {
		*out = peer;
	}
	(void)k_mutex_unlock(&session_lock);

	return on;
}

static void note_send_time(int64_t start)
{
	uint32_t ms = (uint32_t)(k_uptime_get() - start);

	(void)k_mutex_lock(&session_lock, K_FOREVER);
	send_max_ms = MAX(send_max_ms, ms);
	(void)k_mutex_unlock(&session_lock);
}

static void session_update(const struct net_sockaddr_in *from, char cmd)
{
	int64_t now = k_uptime_get();

	(void)k_mutex_lock(&session_lock, K_FOREVER);

	if (cmd == 'X') {
		expires = 0;
	} else {
		if (!session) {
			LOG_INF("Session started");
			imu_stream_enable(true);
			drive_cmds = 0U;
			drive_gap_max_ms = 0U;
			send_max_ms = 0U;
			last_drive = 0;
		}
		session = true;
		peer = *from;
		expires = now + LINK_SESSION_MS;
	}

	if (cmd == 'D') {
		if (last_drive != 0) {
			drive_gap_max_ms = MAX(drive_gap_max_ms, (uint32_t)(now - last_drive));
		}
		last_drive = now;
		drive_cmds++;
	}

	(void)k_mutex_unlock(&session_lock);
}

static void session_expire(void)
{
	bool ended = false;

	(void)k_mutex_lock(&session_lock, K_FOREVER);
	if (session && (k_uptime_get() >= expires)) {
		session = false;
		ended = true;
	}
	(void)k_mutex_unlock(&session_lock);

	if (ended) {
		LOG_INF("Session ended");
		imu_stream_enable(false);
		drive(DRIVE_STOP, 0U);
	}
}

/*
 * Commands have their own thread, so that a send that blocks while Wi-Fi is
 * busy never delays a drive or stop command.
 */
static void rx_thread(void *p1, void *p2, void *p3)
{
	struct net_sockaddr_in local = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(CONFIG_ROBOT_LINK_PORT),
	};
	struct zsock_pollfd pfd;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	if ((sock < 0) ||
	    (zsock_bind(sock, (struct net_sockaddr *)&local, sizeof(local)) < 0)) {
		LOG_ERR("Link socket setup failed (%d)", errno);
		return;
	}

	k_sem_give(&sock_ready);
	health_watch(HEALTH_LINK, true);

	pfd.fd = sock;
	pfd.events = ZSOCK_POLLIN;

	for (;;) {
		health_beat(HEALTH_LINK);

		if (zsock_poll(&pfd, 1, POLL_MS) > 0) {
			struct net_sockaddr_in from;
			net_socklen_t len = sizeof(from);
			char cmd[24];
			ssize_t n = zsock_recvfrom(sock, cmd, sizeof(cmd) - 1U, 0,
						   (struct net_sockaddr *)&from, &len);

			if (n > 0) {
				cmd[n] = '\0';
				if (handle_command(cmd) == 0) {
					session_update(&from, cmd[0]);
				}
			}
		}

		session_expire();
	}
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	int64_t last_imu = 0;
	int64_t next_status = 0;
	uint32_t seq = 0U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_sem_take(&sock_ready, K_FOREVER);

	for (;;) {
		struct net_sockaddr_in to;
		int64_t now;

		k_sleep(K_MSEC(POLL_MS));

		if (!session_peer(&to)) {
			continue;
		}

		now = k_uptime_get();

		while ((imu_stream_pending() >= BATCH_SAMPLES) ||
		       ((imu_stream_pending() > 0U) && ((now - last_imu) >= BATCH_MAX_AGE_MS))) {
			int64_t start = k_uptime_get();

			send_imu(sock, &to, seq++);
			note_send_time(start);
			last_imu = now;
		}

		if (now >= next_status) {
			int64_t start = k_uptime_get();

			next_status = now + STATUS_PERIOD_MS;
			send_status(sock, &to);
			note_send_time(start);
		}
	}
}

/* Commands rank above sending data, which ranks below the network stack. */
K_THREAD_DEFINE(link_rx_tid, LINK_RX_STACK_SIZE, rx_thread, NULL, NULL, NULL, LINK_RX_PRIORITY,
		0, 0);
K_THREAD_DEFINE(link_tx_tid, LINK_TX_STACK_SIZE, tx_thread, NULL, NULL, NULL, LINK_TX_PRIORITY,
		0, 0);
