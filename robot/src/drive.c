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

/*
 * Motor placement, seen from above with the front at the top:
 *
 *   M4  front  M3
 *   M1  rear   M2
 */
static const enum motor_id left_motors[] = {MOTOR_1, MOTOR_4};
static const enum motor_id right_motors[] = {MOTOR_2, MOTOR_3};

/*
 * Motors whose "-1" input turns their wheel backward, from mounting or lead
 * polarity, get their direction reversed here.
 */
static const bool motor_inverted[MOTOR_COUNT] = {
	[MOTOR_1] = false,
	[MOTOR_2] = true,
	[MOTOR_3] = true,
	[MOTOR_4] = false,
};

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
	int32_t speed;
	int32_t target;
	/* Ramp periods left to coast before starting after a reversal. */
	uint32_t pause;
};

static struct side sides[] = {
	{left_motors, ARRAY_SIZE(left_motors)},
	{right_motors, ARRAY_SIZE(right_motors)},
};

static K_MUTEX_DEFINE(drive_lock);

/* Protected by drive_lock. */
static uint32_t inhibit;
static int32_t speed_limit = MOTOR_SPEED_MAX;
static int64_t last_active;

static void side_apply(const struct side *side)
{
	if (side->speed != 0) {
		last_active = k_uptime_get();
	}

	for (size_t i = 0U; i < side->count; i++) {
		enum motor_id id = side->ids[i];
		int err = motor_set(id, motor_inverted[id] ? -side->speed : side->speed);

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

	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	speed = CLAMP(speed, DRIVE_SPEED_MIN, speed_limit);

	if (inhibit != 0U) {
		left = 0;
		right = 0;
	} else if (throttle == 0) {
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

	drive_sides(left, right);

	if ((ms > 0U) && ((left != 0) || (right != 0))) {
		(void)k_work_reschedule(&stop_work, K_MSEC(ms));
	} else {
		(void)k_work_cancel_delayable(&stop_work);
	}

	(void)k_mutex_unlock(&drive_lock);
}

void drive_inhibit_set(uint32_t reasons)
{
	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	if ((inhibit & reasons) != reasons) {
		LOG_WRN("Driving inhibited (0x%x)", (unsigned int)(inhibit | reasons));
	}

	inhibit |= reasons;
	drive_sides(0, 0);
	(void)k_work_cancel_delayable(&stop_work);

	(void)k_mutex_unlock(&drive_lock);
}

void drive_inhibit_clear(uint32_t reasons)
{
	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	if ((inhibit & reasons) != 0U) {
		LOG_INF("Inhibit cleared (0x%x)", (unsigned int)(inhibit & reasons));
	}

	inhibit &= ~reasons;

	(void)k_mutex_unlock(&drive_lock);
}

uint32_t drive_inhibit_get(void)
{
	uint32_t reasons;

	(void)k_mutex_lock(&drive_lock, K_FOREVER);
	reasons = inhibit;
	(void)k_mutex_unlock(&drive_lock);

	return reasons;
}

void drive_speed_limit_set(int32_t limit)
{
	(void)k_mutex_lock(&drive_lock, K_FOREVER);
	speed_limit = CLAMP(limit, DRIVE_SPEED_MIN, MOTOR_SPEED_MAX);
	(void)k_mutex_unlock(&drive_lock);
}

uint32_t drive_idle_ms(void)
{
	int64_t idle = 0;

	(void)k_mutex_lock(&drive_lock, K_FOREVER);

	if ((sides[0].speed == 0) && (sides[1].speed == 0)) {
		idle = k_uptime_get() - last_active;
	}

	(void)k_mutex_unlock(&drive_lock);

	return (uint32_t)MIN(idle, (int64_t)UINT32_MAX);
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
