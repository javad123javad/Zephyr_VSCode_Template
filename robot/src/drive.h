/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_DRIVE_H_
#define ROBOT_DRIVE_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

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

/** Reasons that block driving; drive_inhibit_set() takes a bitmask of them. */
enum drive_inhibit {
	/** A battery is below its cutoff voltage. Clears when it recovers. */
	DRIVE_INHIBIT_BATTERY = BIT(0),
	/** An obstacle is too close. */
	DRIVE_INHIBIT_OBSTACLE = BIT(1),
	/** A collision was detected. Latched until reset by the user. */
	DRIVE_INHIBIT_COLLISION = BIT(2),
};

/** Inhibit reasons that only the user can clear. */
#define DRIVE_INHIBIT_LATCHED DRIVE_INHIBIT_COLLISION

/**
 * @brief Block driving and stop the motors.
 *
 * Drive commands are ignored while any reason is set. Direct motor control
 * (shell "robot motor") is not affected.
 *
 * @param reasons Bitmask of enum drive_inhibit values
 */
void drive_inhibit_set(uint32_t reasons);

/**
 * @brief Allow driving again for the given reasons.
 *
 * @param reasons Bitmask of enum drive_inhibit values
 */
void drive_inhibit_clear(uint32_t reasons);

/**
 * @brief Get the active inhibit reasons.
 *
 * @return Bitmask of enum drive_inhibit values, 0 when driving is allowed
 */
uint32_t drive_inhibit_get(void);

/**
 * @brief Cap the speed of drive commands.
 *
 * @param limit Maximum speed in percent, clamped to DRIVE_SPEED_MIN..100
 */
void drive_speed_limit_set(int32_t limit);

/** What the drive is doing, for logging alongside sensor data. */
struct drive_snapshot {
	/** Last commanded throttle: 1 forward, -1 backward, 0 none. */
	int8_t throttle;
	/** Last commanded turn: 1 right, -1 left, 0 none. */
	int8_t turn;
	/** Last commanded speed in percent. */
	uint8_t speed;
	/** Actual left side speed, -100..100, including ramp and pauses. */
	int8_t left;
	/** Actual right side speed, -100..100. */
	int8_t right;
	/** Active enum drive_inhibit bits. */
	uint8_t inhibit;
};

/**
 * @brief Get the current command and side speeds.
 *
 * The command is the last drive_mix() request; it stays set after the
 * auto-stop, while the side speeds show what the motors actually do.
 *
 * @param snap Filled with the current state
 */
void drive_snapshot_get(struct drive_snapshot *snap);

/**
 * @brief Get how long the motors have been stopped.
 *
 * @return Milliseconds since the motors last ran, 0 while they run
 */
uint32_t drive_idle_ms(void);

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
