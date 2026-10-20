/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_DRIVE_H_
#define ROBOT_DRIVE_H_

#include <stdint.h>

enum drive_move {
	DRIVE_STOP,
	DRIVE_FORWARD,
	DRIVE_BACKWARD,
	DRIVE_LEFT,
	DRIVE_RIGHT,
};

/**
 * @brief Move the robot.
 *
 * Safe to call from any thread.
 *
 * @param move Movement, or DRIVE_STOP to stop all motors
 * @param ms Time after which the robot stops by itself, 0 to run until the
 *           next call
 */
void drive(enum drive_move move, uint32_t ms);

/** Lowest speed accepted by drive_mix(), in percent of full power. */
#define DRIVE_SPEED_MIN 30

/**
 * @brief Move the robot by direction and speed.
 *
 * Holding both a throttle and a turn drives a curve. Safe to call from any
 * thread.
 *
 * @param throttle 1 forward, -1 backward, 0 none
 * @param turn 1 right, -1 left, 0 none; with no throttle the robot spins in
 *             place
 * @param speed Percent of full power, clamped to DRIVE_SPEED_MIN..100
 * @param ms Time after which the robot stops by itself, 0 to run until the
 *           next call
 */
void drive_mix(int32_t throttle, int32_t turn, int32_t speed, uint32_t ms);

/**
 * @brief Parse a movement name.
 *
 * @param name One of "fwd", "back", "left", "right" or "stop"
 * @param len Length of @p name
 * @param move Parsed movement
 *
 * @retval 0 Success
 * @retval -EINVAL Unknown name
 */
int drive_parse(const char *name, size_t len, enum drive_move *move);

#endif /* ROBOT_DRIVE_H_ */
