/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_COLLISION_H_
#define ROBOT_COLLISION_H_

#include <stdint.h>

#include "imu.h"

enum collision_mode {
	/** No detection. */
	COLLISION_OFF,
	/** Detect and report, keep driving. For recording test data. */
	COLLISION_MONITOR,
	/** Detect, stop the motors and latch DRIVE_INHIBIT_COLLISION. */
	COLLISION_STOP,
};

/**
 * Where a hit came from, seen from above, counter-clockwise from the front
 * (x forward, y left): front, front-left, left, back-left, back, back-right,
 * right, front-right.
 */
#define COLLISION_SECTORS 8

struct collision_state {
	enum collision_mode mode;
	/** Thresholds in mg: horizontal acceleration over 25 ms, vibration RMS. */
	uint16_t a25_thr_mg;
	uint16_t vib_thr_mg;
	/** Thresholds while turning; the wheels scrub and shake, 0 = unused. */
	uint16_t a25_turn_thr_mg;
	uint16_t vib_turn_thr_mg;
	/** Collisions detected since boot. */
	uint32_t count;
	/** Last collision: sample time (same clock as the IMU stream). */
	uint32_t t_us;
	/** Last collision: peaks over its first 100 ms, mg. */
	uint16_t a25_mg;
	uint16_t vib_mg;
	/** Last collision: direction it came from, 0..COLLISION_SECTORS-1. */
	uint8_t sector;
};

/**
 * @brief Feed one calibrated IMU sample in the robot frame.
 *
 * Called from the IMU sampling thread for every sample.
 *
 * @param s The sample, with the drive state at sampling time
 */
void collision_process(const struct imu_sample *s);

/**
 * @brief Select what happens on a collision.
 *
 * @param mode New mode
 */
void collision_set_mode(enum collision_mode mode);

/**
 * @brief Set the detection thresholds.
 *
 * @param a25_mg Horizontal acceleration averaged over 25 ms, mg
 * @param vib_mg Vibration RMS over 100 ms, mg
 * @param a25_turn_mg The same while turning (spin or curve), mg
 * @param vib_turn_mg The same while turning, mg; 0 does not use vibration
 */
void collision_set_thresholds(uint16_t a25_mg, uint16_t vib_mg, uint16_t a25_turn_mg,
			      uint16_t vib_turn_mg);

/**
 * @brief Get the mode, thresholds and the last collision.
 *
 * @param st Filled with the current state
 */
void collision_get(struct collision_state *st);

#endif /* ROBOT_COLLISION_H_ */
