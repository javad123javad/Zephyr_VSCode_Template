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
#include "imu.h"
#include "link.h"
#include "wifi.h"

LOG_MODULE_REGISTER(link, LOG_LEVEL_INF);

#define LINK_STACK_SIZE   2048
#define LINK_PRIORITY     K_PRIO_PREEMPT(9)
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

static void send_status(int sock, const struct net_sockaddr_in *peer)
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

	(void)zsock_sendto(sock, &st, sizeof(st), 0, (const struct net_sockaddr *)peer,
			   sizeof(*peer));
}

static void send_imu(int sock, const struct net_sockaddr_in *peer, uint32_t seq)
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

	(void)zsock_sendto(sock, pkt, sizeof(*hdr) + (n * sizeof(struct imu_sample)), 0,
			   (const struct net_sockaddr *)peer, sizeof(*peer));
}

static void link_thread(void *p1, void *p2, void *p3)
{
	struct net_sockaddr_in local = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(CONFIG_ROBOT_LINK_PORT),
	};
	struct net_sockaddr_in peer;
	struct zsock_pollfd pfd;
	int64_t expires = 0;
	int64_t last_imu = 0;
	int64_t next_status = 0;
	uint32_t seq = 0U;
	bool session = false;
	int sock;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	if ((sock < 0) ||
	    (zsock_bind(sock, (struct net_sockaddr *)&local, sizeof(local)) < 0)) {
		LOG_ERR("Link socket setup failed (%d)", errno);
		return;
	}

	pfd.fd = sock;
	pfd.events = ZSOCK_POLLIN;

	for (;;) {
		int64_t now;

		if (zsock_poll(&pfd, 1, POLL_MS) > 0) {
			struct net_sockaddr_in from;
			net_socklen_t len = sizeof(from);
			char cmd[24];
			ssize_t n = zsock_recvfrom(sock, cmd, sizeof(cmd) - 1U, 0,
						   (struct net_sockaddr *)&from, &len);

			if (n > 0) {
				cmd[n] = '\0';
				if (handle_command(cmd) == 0) {
					if (cmd[0] == 'X') {
						expires = 0;
					} else {
						if (!session) {
							LOG_INF("Session started");
							imu_stream_enable(true);
						}
						session = true;
						peer = from;
						expires = k_uptime_get() + LINK_SESSION_MS;
					}
				}
			}
		}

		now = k_uptime_get();

		if (session && (now >= expires)) {
			LOG_INF("Session ended");
			session = false;
			imu_stream_enable(false);
			drive(DRIVE_STOP, 0U);
		}

		if (!session) {
			continue;
		}

		while ((imu_stream_pending() >= BATCH_SAMPLES) ||
		       ((imu_stream_pending() > 0U) && ((now - last_imu) >= BATCH_MAX_AGE_MS))) {
			send_imu(sock, &peer, seq++);
			last_imu = now;
		}

		if (now >= next_status) {
			next_status = now + STATUS_PERIOD_MS;
			send_status(sock, &peer);
		}
	}
}

K_THREAD_DEFINE(link_tid, LINK_STACK_SIZE, link_thread, NULL, NULL, NULL, LINK_PRIORITY, 0, 0);
