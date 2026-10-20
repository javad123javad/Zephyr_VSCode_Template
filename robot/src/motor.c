/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "motor.h"

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

struct motor {
	struct pwm_dt_spec fwd;
	struct pwm_dt_spec rev;
	bool ready;
};

/* The "pwms" property lists each motor's forward then reverse input. */
#define MOTOR_INIT(fwd_idx, rev_idx)                                                               \
	{                                                                                          \
		.fwd = PWM_DT_SPEC_GET_BY_IDX(USER_NODE, fwd_idx),                                 \
		.rev = PWM_DT_SPEC_GET_BY_IDX(USER_NODE, rev_idx),                                 \
	}

static struct motor motors[MOTOR_COUNT] = {
	[MOTOR_1] = MOTOR_INIT(0, 1),
	[MOTOR_2] = MOTOR_INIT(2, 3),
	[MOTOR_3] = MOTOR_INIT(4, 5),
	[MOTOR_4] = MOTOR_INIT(6, 7),
};

static int set_input(const struct pwm_dt_spec *spec, uint32_t percent)
{
	uint32_t pulse = (uint32_t)(((uint64_t)spec->period * percent) / MOTOR_SPEED_MAX);

	return pwm_set_pulse_dt(spec, pulse);
}

int motor_init(void)
{
	int ret = 0;

	for (size_t i = 0U; i < MOTOR_COUNT; i++) {
		struct motor *m = &motors[i];
		int err;

		if (!pwm_is_ready_dt(&m->fwd) || !pwm_is_ready_dt(&m->rev)) {
			LOG_ERR("M%u: PWM controller not ready", (unsigned int)(i + 1U));
			ret = -ENODEV;
			continue;
		}

		err = set_input(&m->fwd, 0U);
		if (err == 0) {
			err = set_input(&m->rev, 0U);
		}
		if (err != 0) {
			LOG_ERR("M%u: PWM setup failed (%d)", (unsigned int)(i + 1U), err);
			ret = -ENODEV;
			continue;
		}

		m->ready = true;
	}

	return ret;
}

int motor_set(enum motor_id id, int32_t speed)
{
	const struct pwm_dt_spec *on;
	const struct pwm_dt_spec *off;
	struct motor *m;
	int err;

	if (((uint32_t)id >= MOTOR_COUNT) || (speed < -MOTOR_SPEED_MAX) ||
	    (speed > MOTOR_SPEED_MAX)) {
		return -EINVAL;
	}

	m = &motors[id];
	if (!m->ready) {
		return -ENODEV;
	}

	on = (speed >= 0) ? &m->fwd : &m->rev;
	off = (speed >= 0) ? &m->rev : &m->fwd;

	/* Release the other input first so the two are never active together. */
	err = set_input(off, 0U);
	if (err == 0) {
		err = set_input(on, (uint32_t)((speed >= 0) ? speed : -speed));
	}

	return (err == 0) ? 0 : -EIO;
}

void motor_stop_all(void)
{
	for (size_t i = 0U; i < MOTOR_COUNT; i++) {
		(void)motor_set((enum motor_id)i, 0);
	}
}
