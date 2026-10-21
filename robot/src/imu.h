/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_IMU_H_
#define ROBOT_IMU_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#include "drive.h"

/** Accelerometer LSB in micro-g: ADXL345 full resolution, 3.9 mg/LSB. */
#define IMU_ACCEL_UG_PER_LSB 3900

/** Gyro sensitivity: ITG3205 at +-2000 deg/s, 14.375 LSB per deg/s. */
#define IMU_GYRO_LSB_PER_DPS_X1000 14375

/*
 * Samples are calibrated and in the robot frame: x forward, y left, z up
 * (ROS REP-103). At rest the accelerometer reads +1 g on z, and a left turn
 * gives a positive z rotation rate.
 */

/** One sample as streamed to the PC, little endian. */
struct imu_sample {
	/** Time of the data-ready interrupt, microseconds since boot (wraps). */
	uint32_t t_us;
	/** Acceleration x, y, z in IMU_ACCEL_UG_PER_LSB units. */
	int16_t accel[3];
	/** Rotation rate x, y, z in raw gyro units. */
	int16_t gyro[3];
	/** Drive command and side speeds at sampling time. */
	struct drive_snapshot drive;
} __packed;

struct imu_stats {
	/** Samples read since boot. */
	uint32_t samples;
	/** Samples read after a missed data-ready interrupt. */
	uint32_t int_timeouts;
	/** Samples dropped because the stream buffer was full. */
	uint32_t dropped;
	/** Samples discarded as corrupted: outside what the sensor can report. */
	uint32_t rejected;
	/** Sampling rate in Hz. */
	uint16_t rate_hz;
	/** Gyro I2C address, 0 when not found. */
	uint8_t gyro_addr;
	/** Accelerometer found. */
	bool accel_ok;
	/** Times the gyro bias was measured at rest since boot. */
	uint32_t bias_updates;
	/** Current gyro bias in the sensor's axes, raw units times 16. */
	int32_t gyro_bias_q4[3];
};

/**
 * @brief Detect and configure the IMU and start sampling.
 *
 * When the accelerometer does not answer, it is looked for again every 2 s
 * and sampling starts once it is found.
 *
 * @retval 0 Started
 * @retval -ENODEV Not found yet, or I2C bus not ready
 */
int imu_start(void);

/**
 * @brief Start or stop queueing samples for streaming.
 *
 * Turning it on or off empties the queue. While it is off, samples are only
 * kept as the latest sample.
 *
 * @param on True to queue samples
 */
void imu_stream_enable(bool on);

/**
 * @brief Take queued samples, oldest first.
 *
 * @param out Buffer for the samples
 * @param max Capacity of @p out
 *
 * @return Number of samples copied
 */
size_t imu_stream_take(struct imu_sample *out, size_t max);

/**
 * @brief Get the number of queued samples.
 *
 * @return Samples waiting in the queue
 */
size_t imu_stream_pending(void);

/**
 * @brief Get sampling statistics and the latest sample.
 *
 * @param stats Filled with the statistics
 * @param last Filled with the latest sample, may be NULL
 */
void imu_get(struct imu_stats *stats, struct imu_sample *last);

#endif /* ROBOT_IMU_H_ */
