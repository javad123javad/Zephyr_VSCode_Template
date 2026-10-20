/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "drive.h"
#include "motor.h"

LOG_MODULE_REGISTER(drive, LOG_LEVEL_INF);

/* Motor placement: M1/M2 on the left side, M3/M4 on the right side. */
static const enum motor_id left_motors[] = {MOTOR_1, MOTOR_2};
static const enum motor_id right_motors[] = {MOTOR_3, MOTOR_4};

/*
 * The two sides are mounted mirrored, so a side whose "-1" input turns its
 * wheels backward gets its direction reversed here.
 */
#define LEFT_INVERTED  true
#define RIGHT_INVERTED false

/*
 * Soft start: a side starts at RAMP_START_SPEED, the lowest power that still
 * turns the geared motors, and reaches full speed after RAMP_MS. Stops and
 * slowing down are immediate.
 */
#define RAMP_START_SPEED 35
#define RAMP_MS          600
#define RAMP_PERIOD_MS   20
#define RAMP_STEP        (((MOTOR_SPEED_MAX - RAMP_START_SPEED) * RAMP_PERIOD_MS) / RAMP_MS)

/*
 * A side that reverses coasts for this long before starting in the new
 * direction, so the gearboxes are not loaded with a sudden reversal.
 */
#define REVERSE_PAUSE_MS 150

/* In a curve the inner side runs at this share of the outer side's speed. */
#define DRIVE_CURVE_INNER_PCT 30

struct side {
	const enum motor_id *ids;
	size_t count;
	bool inverted;
	int32_t speed;
	int32_t target;
	/* Ramp periods left to coast before starting after a reversal. */
	uint32_t pause;
};

static struct side sides[] = {
	{left_motors, ARRAY_SIZE(left_motors), LEFT_INVERTED},
	{right_motors, ARRAY_SIZE(right_motors), RIGHT_INVERTED},
};

static K_MUTEX_DEFINE(drive_lock);

static void side_apply(const struct side *side)
{
	int32_t speed = side->inverted ? -side->speed : side->speed;

	for (size_t i = 0U; i < side->count; i++) {
		int err = motor_set(side->ids[i], speed);

		if (err != 0) {
			LOG_WRN("M%u: set failed (%d)", (unsigned int)(side->ids[i] + 1), err);
		}
	}
}

static bool side_busy(const struct side *side)
{
	return (side->speed != side->target) || (side->pause > 0U);
}

/* Start from rest at the ramp start speed, or at the target if it is lower. */
static void side_start(struct side *side)
{
	int32_t start = MIN(RAMP_START_SPEED, (side->target >= 0) ? side->target : -side->target);

	side->speed = (side->target >= 0) ? start : -start;
	side_apply(side);
}

/* Advance one ramp period; returns true while the side is still busy. */
static bool side_step(struct side *side)
{
	if (side->pause > 0U) {
		side->pause--;
		if (side->pause == 0U) {
			side_start(side);
		}
		return side_busy(side);
	}

	if (side->speed == side->target) {
		return false;
	}

	if (side->speed < side->target) {
		side->speed = MIN(side->speed + RAMP_STEP, side->target);
	} else {
		side->speed = MAX(side->speed - RAMP_STEP, side->target);
	}

	side_apply(side);

	return side_busy(side);
}

/*
 * Starting begins at the ramp start speed. Reversing coasts for
 * REVERSE_PAUSE_MS first. Slowing down, as for the inner side of a curve or a
 * lower speed setting, takes effect at once; only speeding up is ramped.
 */
static void side_set_target(struct side *side, int32_t target)
{
	side->target = target;

	if (target == 0) {
		side->pause = 0U;
		side->speed = 0;
		side_apply(side);
	} else if (side->pause > 0U) {
		/* Already coasting after a reversal; side_step() starts it. */
	} else if (side->speed == 0) {
		side_start(side);
	} else if ((target > 0) != (side->speed > 0)) {
		side->speed = 0;
		side->pause = DIV_ROUND_UP(REVERSE_PAUSE_MS, RAMP_PERIOD_MS);
		side_apply(side);
	} else if (((target > 0) && (target < side->speed)) ||
		   ((target < 0) && (target > side->speed))) {
		side->speed = target;
		side_apply(side);
	} else {
		/* Same direction and faster: side_step() ramps up. */
	}
}

static void ramp_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ramp_work, ramp_work_handler);

static void ramp_work_handler(struct k_work *work)
{
	bool ramping = false;

	ARG_UNUSED(work);

	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	for (size_t i = 0U; i < ARRAY_SIZE(sides); i++) {
		ramping = side_step(&sides[i]) || ramping;
	}

	if (ramping) {
		(void)k_work_schedule(&ramp_work, K_MSEC(RAMP_PERIOD_MS));
	}

	(void)k_mutex_unlock(&drive_lock);
}

static void drive_sides(int32_t left, int32_t right)
{
	side_set_target(&sides[0], left);
	side_set_target(&sides[1], right);

	if (side_busy(&sides[0]) || side_busy(&sides[1])) {
		(void)k_work_schedule(&ramp_work, K_MSEC(RAMP_PERIOD_MS));
	} else {
		(void)k_work_cancel_delayable(&ramp_work);
	}
}

static void stop_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)k_mutex_lock(&drive_lock, K_FOREVER);
	drive_sides(0, 0);
	(void)k_mutex_unlock(&drive_lock);
}

static K_WORK_DELAYABLE_DEFINE(stop_work, stop_work_handler);

void drive_mix(int32_t throttle, int32_t turn, int32_t speed, uint32_t ms)
{
	int32_t left;
	int32_t right;

	throttle = CLAMP(throttle, -1, 1);
	turn = CLAMP(turn, -1, 1);
	speed = CLAMP(speed, DRIVE_SPEED_MIN, MOTOR_SPEED_MAX);

	if (throttle == 0) {
		/* Spin in place; turn > 0 is clockwise. */
		left = turn * speed;
		right = -turn * speed;
	} else {
		/* Straight, or a curve with the inner side slowed down. */
		int32_t inner = (speed * DRIVE_CURVE_INNER_PCT) / 100;

		/* Below the minimum the motor only hums, so let it coast. */
		if (inner < DRIVE_SPEED_MIN) {
			inner = 0;
		}

		left = throttle * ((turn < 0) ? inner : speed);
		right = throttle * ((turn > 0) ? inner : speed);
	}

	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	drive_sides(left, right);

	if ((ms > 0U) && ((left != 0) || (right != 0))) {
		(void)k_work_reschedule(&stop_work, K_MSEC(ms));
	} else {
		(void)k_work_cancel_delayable(&stop_work);
	}

	(void)k_mutex_unlock(&drive_lock);
}

void drive(enum drive_move move, uint32_t ms)
{
	switch (move) {
	case DRIVE_FORWARD:
		drive_mix(1, 0, MOTOR_SPEED_MAX, ms);
		break;
	case DRIVE_BACKWARD:
		drive_mix(-1, 0, MOTOR_SPEED_MAX, ms);
		break;
	case DRIVE_LEFT:
		drive_mix(0, -1, MOTOR_SPEED_MAX, ms);
		break;
	case DRIVE_RIGHT:
		drive_mix(0, 1, MOTOR_SPEED_MAX, ms);
		break;
	default:
		drive_mix(0, 0, MOTOR_SPEED_MAX, 0U);
		break;
	}
}

int drive_parse(const char *name, size_t len, enum drive_move *move)
{
	static const struct {
		const char *name;
		enum drive_move move;
	} moves[] = {
		{"fwd", DRIVE_FORWARD},
		{"back", DRIVE_BACKWARD},
		{"left", DRIVE_LEFT},
		{"right", DRIVE_RIGHT},
		{"stop", DRIVE_STOP},
	};

	for (size_t i = 0U; i < ARRAY_SIZE(moves); i++) {
		if ((strlen(moves[i].name) == len) && (memcmp(moves[i].name, name, len) == 0)) {
			*move = moves[i].move;
			return 0;
		}
	}

	return -EINVAL;
}
