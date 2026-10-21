/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Collision detection on the IMU stream, the rule tools/analyze.py checks:
 * while driving, a collision is the horizontal acceleration averaged over
 * 25 ms (a25) or the vibration RMS over 100 ms (vib) above its threshold.
 * Averaging hides the driving vibration; the context hides braking, which
 * reaches similar values right after a stop. Turning has its own
 * thresholds: the skid-steer wheels scrub sideways and shake the robot about
 * as hard as a collision, so by default only a25 counts there.
 */

#include <math.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "collision.h"
#include "drive.h"

LOG_MODULE_REGISTER(collision, LOG_LEVEL_INF);

#define WIN_A25      10   /* 25 ms at 400 Hz */
#define WIN_VIB      40   /* 100 ms */
#define WARMUP       WIN_VIB
/* Starting and direction changes shake the robot; driving begins after. */
#define START_US     800000U
/* Direction from the first 20 ms of contact, peaks from the first 100 ms. */
#define DIR_SAMPLES  8
#define PEAK_SAMPLES 40
/* In monitor mode, one report per collision, not one per sample. */
#define HOLDOFF_US   1000000U
/* Rest offset (tilt) learns with a time constant of 256 samples. */
#define OFS_SHIFT    8

#define LSB_TO_MG(x) (((x) * IMU_ACCEL_UG_PER_LSB) / 1000)
#define MG_TO_LSB(x) (((x) * 1000) / IMU_ACCEL_UG_PER_LSB)

static struct k_spinlock lock;
static struct collision_state state = {
	.mode = CONFIG_ROBOT_COLLISION_MODE,
	.a25_thr_mg = CONFIG_ROBOT_COLLISION_A25_MG,
	.vib_thr_mg = CONFIG_ROBOT_COLLISION_VIB_MG,
	.a25_turn_thr_mg = CONFIG_ROBOT_COLLISION_A25_TURN_MG,
	.vib_turn_thr_mg = CONFIG_ROBOT_COLLISION_VIB_TURN_MG,
};

/* Used by the sampling thread only. */
static int16_t win_x[WIN_A25];
static int16_t win_y[WIN_A25];
static int32_t sum_x;
static int32_t sum_y;
static uint32_t win_q[WIN_VIB];
static uint64_t sum_q;
static uint32_t n_samples;
static int32_t ofs_x_q;
static int32_t ofs_y_q;
static int prev_sides;
static uint32_t move_start_us;
static uint32_t holdoff_until_us;

/* Event being completed: direction and peaks over its first samples. */
static bool collecting;
static uint32_t ev_n;
static uint32_t ev_t_us;
static int32_t ev_dir_x;
static int32_t ev_dir_y;
static uint32_t ev_a25_sq;
static uint32_t ev_vib_sq;

static int sign(int v)
{
	return (v > 0) - (v < 0);
}

static uint8_t to_sector(int32_t x, int32_t y)
{
	/* The acceleration points away from what was hit. */
	float ang = atan2f((float)-y, (float)-x) * (180.0f / 3.14159265f);

	if (ang < 0.0f) {
		ang += 360.0f;
	}

	return (uint8_t)((uint32_t)((ang + 22.5f) / 45.0f) % COLLISION_SECTORS);
}

static void finish_event(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.count++;
	state.t_us = ev_t_us;
	state.a25_mg = (uint16_t)MIN(LSB_TO_MG((uint32_t)sqrtf((float)ev_a25_sq)), UINT16_MAX);
	state.vib_mg = (uint16_t)MIN(LSB_TO_MG((uint32_t)sqrtf((float)ev_vib_sq)), UINT16_MAX);
	state.sector = to_sector(ev_dir_x, ev_dir_y);
	k_spin_unlock(&lock, key);

	LOG_WRN("Collision: a25 %u mg, vib %u mg, sector %u", state.a25_mg, state.vib_mg,
		state.sector);
}

void collision_process(const struct imu_sample *s)
{
	const struct drive_snapshot *d = &s->drive;
	bool moving = (d->left != 0) || (d->right != 0);
	bool commanded = (d->throttle != 0) || (d->turn != 0);
	int sides = (sign(d->left) * 3) + sign(d->right);
	struct collision_state cfg;
	int32_t ax;
	int32_t ay;
	int32_t mx;
	int32_t my;
	uint32_t a25_sq;
	uint32_t vib_sq;
	size_t i;
	bool driving;
	bool turning;

	collision_get(&cfg);

	/* Learn the tilt offset only at rest. */
	if (!moving) {
		ofs_x_q += (((int32_t)s->accel[0] << OFS_SHIFT) - ofs_x_q) >> OFS_SHIFT;
		ofs_y_q += (((int32_t)s->accel[1] << OFS_SHIFT) - ofs_y_q) >> OFS_SHIFT;
	}
	ax = s->accel[0] - (ofs_x_q >> OFS_SHIFT);
	ay = s->accel[1] - (ofs_y_q >> OFS_SHIFT);

	i = n_samples % WIN_A25;
	sum_x += ax - win_x[i];
	sum_y += ay - win_y[i];
	win_x[i] = (int16_t)ax;
	win_y[i] = (int16_t)ay;
	mx = sum_x / WIN_A25;
	my = sum_y / WIN_A25;
	a25_sq = (uint32_t)((mx * mx) + (my * my));

	i = n_samples % WIN_VIB;
	sum_q -= win_q[i];
	win_q[i] = (uint32_t)(((ax - mx) * (ax - mx)) + ((ay - my) * (ay - my)));
	sum_q += win_q[i];
	vib_sq = (uint32_t)(sum_q / WIN_VIB);

	n_samples++;

	if ((sides != prev_sides) && moving) {
		move_start_us = s->t_us;
	}
	prev_sides = sides;
	driving = moving && commanded && ((uint32_t)(s->t_us - move_start_us) >= START_US);
	turning = driving && (d->turn != 0);

	if (collecting) {
		if (ev_n < DIR_SAMPLES) {
			ev_dir_x += mx;
			ev_dir_y += my;
		}
		ev_a25_sq = MAX(ev_a25_sq, a25_sq);
		ev_vib_sq = MAX(ev_vib_sq, vib_sq);
		if (++ev_n == PEAK_SAMPLES) {
			collecting = false;
			finish_event();
		}
		return;
	}

	if ((cfg.mode == COLLISION_OFF) || (n_samples < WARMUP) || !driving ||
	    ((int32_t)(s->t_us - holdoff_until_us) < 0)) {
		return;
	}

	{
		uint32_t a25_thr = MG_TO_LSB((uint32_t)(turning ? cfg.a25_turn_thr_mg
							       : cfg.a25_thr_mg));
		uint32_t vib_mg = turning ? cfg.vib_turn_thr_mg : cfg.vib_thr_mg;
		uint32_t vib_thr = MG_TO_LSB(vib_mg);
		bool hit = a25_sq > (a25_thr * a25_thr);

		if ((vib_mg > 0U) && (vib_sq > (vib_thr * vib_thr))) {
			hit = true;
		}
		if (!hit) {
			return;
		}
	}

	/* Stop first; the direction and peaks follow over the next samples. */
	if (cfg.mode == COLLISION_STOP) {
		drive_inhibit_set(DRIVE_INHIBIT_COLLISION);
	}

	collecting = true;
	ev_n = 0U;
	ev_t_us = s->t_us;
	ev_dir_x = 0;
	ev_dir_y = 0;
	ev_a25_sq = a25_sq;
	ev_vib_sq = vib_sq;
	holdoff_until_us = s->t_us + HOLDOFF_US;
}

void collision_set_mode(enum collision_mode mode)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.mode = mode;
	k_spin_unlock(&lock, key);
}

void collision_set_thresholds(uint16_t a25_mg, uint16_t vib_mg, uint16_t a25_turn_mg,
			      uint16_t vib_turn_mg)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.a25_thr_mg = a25_mg;
	state.vib_thr_mg = vib_mg;
	state.a25_turn_thr_mg = a25_turn_mg;
	state.vib_turn_thr_mg = vib_turn_mg;
	k_spin_unlock(&lock, key);
}

void collision_get(struct collision_state *st)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*st = state;
	k_spin_unlock(&lock, key);
}
