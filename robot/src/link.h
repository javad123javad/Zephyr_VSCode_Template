/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UDP link to the PC app on port CONFIG_ROBOT_LINK_PORT.
 *
 * PC to robot, ASCII text, one command per datagram. Every command makes the
 * sender the peer and keeps the session alive for LINK_SESSION_MS:
 *   "S"                        keep the session alive and stream data
 *   "D <throttle> <turn> <speed>"  drive, e.g. "D 1 -1 80"; runs 700 ms
 *   "R"                        clear latched drive inhibits (collision)
 *   "M <mode>"                 collision detection: 0 off, 1 monitor, 2 stop
 *   "T <a25> <vib> [<a25 turning> <vib turning>]"
 *                              collision thresholds in mg; vib turning 0 =
 *                              vibration unused while turning
 *   "C <ctx>"                  acting detector: 0 drive context, 1 IMU only
 *   "I <a25> <vib> <a25 turning> <turn deg/s>"
 *                              IMU-only detector thresholds; vib 0 = unused
 *   "X"                        end the session
 *
 * Robot to PC, binary, little endian: struct link_imu_header followed by
 * count struct imu_sample, and struct link_status twice a second.
 */

#ifndef ROBOT_LINK_H_
#define ROBOT_LINK_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#define LINK_IMU_MAGIC    0x31554D49U /* "IMU1" */
#define LINK_STATUS_MAGIC 0x31415453U /* "STA1" */

/** A session ends this long after the last command from the peer. */
#define LINK_SESSION_MS 3000

struct link_imu_header {
	uint32_t magic;
	uint32_t seq;
	/** Samples dropped on the robot since boot because the queue was full. */
	uint32_t dropped;
	/** Samples read without their data-ready interrupt since boot. */
	uint32_t int_timeouts;
	uint16_t rate_hz;
	uint8_t count;
	/** sizeof(struct imu_sample), for the PC to check the layout. */
	uint8_t sample_size;
} __packed;

struct link_status {
	uint32_t magic;
	uint8_t version[3];
	int8_t rssi;
	/** Active enum drive_inhibit bits. */
	uint8_t inhibit;
	/** Bit 0 motor pack low, bit 1 board cell low. */
	uint8_t battery_low;
	uint8_t imu_ok;
	uint8_t reserved;
	/** Battery voltages in mV, -1 when not measured. */
	int32_t motor_mv;
	int32_t board_mv;
	uint32_t imu_samples;
	uint32_t imu_int_timeouts;
	uint32_t imu_dropped;
	uint32_t gyro_bias_updates;
	/** Gyro bias in the sensor's axes, milli-deg/s. */
	int32_t gyro_bias_mdps[3];
	uint32_t uptime_ms;
	/** Drive commands received in this session. */
	uint32_t drive_cmds;
	/** Longest gap between two drive commands in this session, ms. */
	uint32_t drive_gap_max_ms;
	/** Longest time one send blocked in this session, ms. */
	uint32_t send_max_ms;
	/** IMU samples discarded as corrupted since boot. */
	uint32_t imu_rejected;
	/** Collision detection: enum collision_mode and thresholds in mg. */
	uint8_t col_mode;
	/** Last collision: direction it came from, see COLLISION_SECTORS. */
	uint8_t col_sector;
	uint16_t col_a25_thr_mg;
	uint16_t col_vib_thr_mg;
	/** Last collision: peaks in mg. */
	uint16_t col_a25_mg;
	uint16_t col_vib_mg;
	uint16_t reserved2;
	/** Collisions since boot; a change means a new one. */
	uint32_t col_count;
	/** Last collision: sample time, same clock as the IMU stream. */
	uint32_t col_t_us;
	/** Collision thresholds while turning, mg; vibration 0 = unused. */
	uint16_t col_a25_turn_thr_mg;
	uint16_t col_vib_turn_thr_mg;
	/**
	 * The col_ fields above are the drive-context detector. The IMU-only
	 * detector follows; col_acting is enum collision_context.
	 */
	uint8_t col_acting;
	uint8_t imu_col_sector;
	uint16_t imu_col_a25_thr_mg;
	uint16_t imu_col_vib_thr_mg;
	uint16_t imu_col_a25_turn_thr_mg;
	uint16_t imu_col_turn_dps;
	uint16_t imu_col_a25_mg;
	uint16_t imu_col_vib_mg;
	uint16_t reserved3;
	uint32_t imu_col_count;
	uint32_t imu_col_t_us;
} __packed;

#endif /* ROBOT_LINK_H_ */
