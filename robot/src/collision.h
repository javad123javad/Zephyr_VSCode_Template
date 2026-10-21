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
	/** The acting detector stops the motors and latches DRIVE_INHIBIT_COLLISION. */
	COLLISION_STOP,
};

/**
 * Two detectors run on the same features and differ in where their context
 * comes from. One acts (stops the robot in stop mode), the other only
 * reports, so that both can be compared on the same drives.
 */
enum collision_context {
	/** Context from the drive: command and side speeds. */
	COLLISION_CTX_DRIVE,
	/** Context from the IMU alone, as on a vehicle without access to its drive. */
	COLLISION_CTX_IMU,
	COLLISION_CTX_COUNT,
};

/**
 * Where a hit came from, seen from above, counter-clockwise from the front
 * (x forward, y left): front, front-left, left, back-left, back, back-right,
 * right, front-right.
 */
#define COLLISION_SECTORS 8

struct collision_event {
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

struct collision_state {
	enum collision_mode mode;
	/** Detector that stops the robot in stop mode. */
	enum collision_context acting;
	/** Drive context: thresholds in mg, while straight and while turning. */
	uint16_t a25_thr_mg;
	uint16_t vib_thr_mg;
	uint16_t a25_turn_thr_mg;
	/** 0: vibration not used while turning. */
	uint16_t vib_turn_thr_mg;
	/** IMU context: thresholds in mg, and the yaw rate that means turning. */
	uint16_t imu_a25_thr_mg;
	uint16_t imu_vib_thr_mg;
	uint16_t imu_a25_turn_thr_mg;
	uint16_t imu_turn_dps;
	/** Last collision of each detector. */
	struct collision_event ev[COLLISION_CTX_COUNT];
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
 * @brief Select the detector that acts; the other one only reports.
 *
 * @param ctx Detector
 */
void collision_set_acting(enum collision_context ctx);

/**
 * @brief Set the thresholds of the drive-context detector.
 *
 * @param a25_mg Horizontal acceleration averaged over 25 ms, mg
 * @param vib_mg Vibration RMS over 100 ms, mg
 * @param a25_turn_mg The same while turning (spin or curve), mg
 * @param vib_turn_mg The same while turning, mg; 0 does not use vibration
 */
void collision_set_thresholds(uint16_t a25_mg, uint16_t vib_mg, uint16_t a25_turn_mg,
			      uint16_t vib_turn_mg);

/**
 * @brief Set the thresholds of the IMU-only detector.
 *
 * @param a25_mg Horizontal acceleration averaged over 25 ms, mg
 * @param vib_mg Vibration RMS over 100 ms, mg
 * @param a25_turn_mg Averaged acceleration while turning, mg
 * @param turn_dps Yaw rate averaged over 250 ms above which the robot turns
 */
void collision_set_imu_thresholds(uint16_t a25_mg, uint16_t vib_mg, uint16_t a25_turn_mg,
				  uint16_t turn_dps);

/**
 * @brief Get the mode, thresholds and the last collision of each detector.
 *
 * @param st Filled with the current state
 */
void collision_get(struct collision_state *st);

#endif /* ROBOT_COLLISION_H_ */
