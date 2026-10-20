/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "drive.h"
#include "motor.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

#define SAMPLE_PERIOD_MS 500

/* Thresholds are evaluated only after the motors have rested this long. */
#define IDLE_MS 1000

/* While driving only a hard floor applies, for this many samples in a row. */
#define HARD_SAMPLES 6

/* Speed cap while a battery is low. */
#define LOW_SPEED_LIMIT 60

/* Below this per cell the pack is switched off or not connected. */
#define ABSENT_MV_PER_CELL 1000

struct battery_cfg {
	struct adc_dt_spec adc;
	uint32_t div_top;
	uint32_t div_bottom;
	uint32_t cal;
	uint32_t cells;
	/* Per-cell levels in mV; each "clear" level adds hysteresis. */
	uint32_t low_mv;
	uint32_t low_clear_mv;
	uint32_t empty_mv;
	uint32_t empty_clear_mv;
	uint32_t hard_mv;
	/* The battery powers the motors, so its voltage sags while driving. */
	bool motor_load;
};

static const struct battery_cfg cfgs[BATTERY_COUNT] = {
	[BATTERY_MOTOR] = {
		.adc = ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 0),
		.div_top = CONFIG_ROBOT_BATT_MOTOR_DIV_TOP,
		.div_bottom = CONFIG_ROBOT_BATT_MOTOR_DIV_BOTTOM,
		.cal = CONFIG_ROBOT_BATT_MOTOR_CAL,
		.cells = CONFIG_ROBOT_BATT_MOTOR_CELLS,
		.low_mv = 3400,
		.low_clear_mv = 3500,
		.empty_mv = 3200,
		.empty_clear_mv = 3400,
		.hard_mv = 3000,
		.motor_load = true,
	},
	[BATTERY_BOARD] = {
		.adc = ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 1),
		.div_top = CONFIG_ROBOT_BATT_BOARD_DIV_TOP,
		.div_bottom = CONFIG_ROBOT_BATT_BOARD_DIV_BOTTOM,
		.cal = CONFIG_ROBOT_BATT_BOARD_CAL,
		.cells = 1,
		.low_mv = 3500,
		.low_clear_mv = 3600,
		.empty_mv = 3300,
		.empty_clear_mv = 3500,
		.hard_mv = 3100,
		.motor_load = false,
	},
};

struct battery_data {
	struct battery_state state;
	bool valid;
	uint32_t hard_count;
};

static struct battery_data data[BATTERY_COUNT];
static K_MUTEX_DEFINE(battery_lock);

static int measure(const struct battery_cfg *cfg, uint32_t *mv)
{
	uint16_t raw;
	int32_t val;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int err;

	err = adc_sequence_init_dt(&cfg->adc, &seq);
	if (err == 0) {
		err = adc_read_dt(&cfg->adc, &seq);
	}
	if (err != 0) {
		return err;
	}

	val = (int32_t)raw;
	err = adc_raw_to_millivolts_dt(&cfg->adc, &val);
	if (err != 0) {
		return err;
	}

	*mv = (uint32_t)(((uint64_t)MAX(val, 0) * (cfg->div_top + cfg->div_bottom) * cfg->cal) /
			 ((uint64_t)cfg->div_bottom * 1000U));

	return 0;
}

/* Update low/empty with hysteresis from the filtered voltage. */
static void evaluate(const struct battery_cfg *cfg, struct battery_data *d, bool idle)
{
	uint32_t cell_mv = d->state.mv / cfg->cells;

	if (cell_mv < ABSENT_MV_PER_CELL) {
		d->state.low = false;
		d->state.empty = false;
		d->hard_count = 0U;
		return;
	}

	if (idle) {
		d->hard_count = 0U;

		if (cell_mv < cfg->low_mv) {
			d->state.low = true;
		} else if (cell_mv > cfg->low_clear_mv) {
			d->state.low = false;
		}

		if (cell_mv < cfg->empty_mv) {
			d->state.empty = true;
		} else if (cell_mv > cfg->empty_clear_mv) {
			d->state.empty = false;
		}
	} else if (cell_mv < cfg->hard_mv) {
		d->hard_count++;
		if (d->hard_count >= HARD_SAMPLES) {
			d->state.empty = true;
		}
	} else {
		d->hard_count = 0U;
	}
}

static void sample_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_work_handler);

static void sample_work_handler(struct k_work *work)
{
	bool motors_idle = drive_idle_ms() >= IDLE_MS;
	bool any_low = false;
	bool any_empty = false;

	ARG_UNUSED(work);

	(void)k_mutex_lock(&battery_lock, K_FOREVER);

	for (size_t i = 0U; i < BATTERY_COUNT; i++) {
		const struct battery_cfg *cfg = &cfgs[i];
		struct battery_data *d = &data[i];
		uint32_t mv;

		if (measure(cfg, &mv) != 0) {
			continue;
		}

		/* First sample sets the filter, then a 1/4 exponential average. */
		d->state.mv = d->valid ? ((3U * d->state.mv) + mv) / 4U : mv;
		d->valid = true;

		evaluate(cfg, d, motors_idle || !cfg->motor_load);

		any_low = any_low || d->state.low;
		any_empty = any_empty || d->state.empty;
	}

	(void)k_mutex_unlock(&battery_lock);

	if (any_empty) {
		if ((drive_inhibit_get() & DRIVE_INHIBIT_BATTERY) == 0U) {
			drive_inhibit_set(DRIVE_INHIBIT_BATTERY);
		}
	} else {
		drive_inhibit_clear(DRIVE_INHIBIT_BATTERY);
	}

	drive_speed_limit_set(any_low ? LOW_SPEED_LIMIT : MOTOR_SPEED_MAX);

	(void)k_work_schedule(&sample_work, K_MSEC(SAMPLE_PERIOD_MS));
}

int battery_start(void)
{
	for (size_t i = 0U; i < BATTERY_COUNT; i++) {
		int err;

		if (!adc_is_ready_dt(&cfgs[i].adc)) {
			LOG_ERR("ADC not ready");
			return -ENODEV;
		}

		err = adc_channel_setup_dt(&cfgs[i].adc);
		if (err != 0) {
			LOG_ERR("ADC channel %u setup failed (%d)", cfgs[i].adc.channel_id, err);
			return err;
		}
	}

	(void)k_work_schedule(&sample_work, K_NO_WAIT);

	return 0;
}

int battery_get(enum battery_id id, struct battery_state *state)
{
	int ret = -ENODATA;

	if ((uint32_t)id >= BATTERY_COUNT) {
		return -ENODATA;
	}

	(void)k_mutex_lock(&battery_lock, K_FOREVER);
	if (data[id].valid) {
		*state = data[id].state;
		ret = 0;
	}
	(void)k_mutex_unlock(&battery_lock);

	return ret;
}
