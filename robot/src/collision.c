/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Collision detection on the IMU stream, the rule tools/analyze.py checks and
 * doc/collision-detection.rst derives. Two features on every sample:
 *
 *   a25  horizontal acceleration averaged over 25 ms
 *   vib  vibration RMS over 100 ms, of what the 25 ms average leaves out
 *
 * Two detectors use them with different context:
 *
 * Drive context: while driving straight, a25 or vib above its threshold;
 * while turning (turn command), a25 only, because the skid-steer wheels
 * scrub and shake the robot as hard as a collision. Braking is excluded
 * because the sides have stopped by then.
 *
 * IMU context, as on a vehicle without access to its drive: turning is a yaw
 * rate averaged over 250 ms above a threshold, held for 500 ms because the
 * scrubbing turns the robot in jerks. Braking is excluded by higher
 * thresholds only: it is slower than an impact.
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
#define WIN_YAW      100  /* 250 ms */
#define WARMUP       WIN_YAW
/* Starting and direction changes shake the robot; driving begins after. */
#define START_US     800000U
/* IMU context: a turn lasts this long after the yaw rate dropped. */
#define TURN_HOLD_US 500000U
/* Direction from the first 20 ms of contact, peaks from the first 100 ms. */
#define DIR_SAMPLES  8
#define PEAK_SAMPLES 40
/* One report per collision, not one per sample. */
#define HOLDOFF_US   1000000U
/* Rest offset (tilt) learns with a time constant of 256 samples. */
#define OFS_SHIFT    8

#define LSB_TO_MG(x) (((x) * IMU_ACCEL_UG_PER_LSB) / 1000)
#define MG_TO_LSB(x) (((x) * 1000) / IMU_ACCEL_UG_PER_LSB)

static const char *const ctx_names[COLLISION_CTX_COUNT] = {"drive", "IMU"};

static struct k_spinlock lock;
static struct collision_state state = {
	.mode = CONFIG_ROBOT_COLLISION_MODE,
	.acting = CONFIG_ROBOT_COLLISION_ACTING,
	.a25_thr_mg = CONFIG_ROBOT_COLLISION_A25_MG,
	.vib_thr_mg = CONFIG_ROBOT_COLLISION_VIB_MG,
	.a25_turn_thr_mg = CONFIG_ROBOT_COLLISION_A25_TURN_MG,
	.vib_turn_thr_mg = CONFIG_ROBOT_COLLISION_VIB_TURN_MG,
	.imu_a25_thr_mg = CONFIG_ROBOT_COLLISION_IMU_A25_MG,
	.imu_vib_thr_mg = CONFIG_ROBOT_COLLISION_IMU_VIB_MG,
	.imu_a25_turn_thr_mg = CONFIG_ROBOT_COLLISION_IMU_A25_TURN_MG,
	.imu_turn_dps = CONFIG_ROBOT_COLLISION_IMU_TURN_DPS,
};

/* Feature state, used by the sampling thread only. */
static int16_t win_x[WIN_A25];
static int16_t win_y[WIN_A25];
static int32_t sum_x;
static int32_t sum_y;
static uint32_t win_q[WIN_VIB];
static uint64_t sum_q;
static int16_t win_yaw[WIN_YAW];
static int32_t sum_yaw;
static uint32_t n_samples;
static int32_t ofs_x_q;
static int32_t ofs_y_q;
static int prev_sides;
static uint32_t move_start_us;
static uint32_t turn_seen_us;
static bool turn_seen;

/* Per detector: the event being completed and the hold-off. */
struct detector {
	bool collecting;
	uint32_t n;
	uint32_t t_us;
	int32_t dir_x;
	int32_t dir_y;
	uint32_t a25_sq;
	uint32_t vib_sq;
	/* Within HOLDOFF_US of t_us, the last event's start. */
	bool holdoff;
};

static struct detector det[COLLISION_CTX_COUNT];

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

static void finish_event(size_t i)
{
	struct detector *d = &det[i];
	struct collision_event ev;
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.ev[i].count++;
	state.ev[i].t_us = d->t_us;
	state.ev[i].a25_mg = (uint16_t)MIN(LSB_TO_MG((uint32_t)sqrtf((float)d->a25_sq)),
					   UINT16_MAX);
	state.ev[i].vib_mg = (uint16_t)MIN(LSB_TO_MG((uint32_t)sqrtf((float)d->vib_sq)),
					   UINT16_MAX);
	state.ev[i].sector = to_sector(d->dir_x, d->dir_y);
	ev = state.ev[i];
	k_spin_unlock(&lock, key);

	LOG_WRN("Collision (%s context): a25 %u mg, vib %u mg, sector %u", ctx_names[i],
		ev.a25_mg, ev.vib_mg, ev.sector);
}

static bool over(uint32_t value_sq, uint32_t thr_mg)
{
	uint32_t thr = MG_TO_LSB(thr_mg);

	return (thr_mg > 0U) && (value_sq > (thr * thr));
}

void collision_process(const struct imu_sample *s)
{
	const struct drive_snapshot *d = &s->drive;
	bool moving = (d->left != 0) || (d->right != 0);
	bool commanded = (d->throttle != 0) || (d->turn != 0);
	int sides = (sign(d->left) * 3) + sign(d->right);
	struct collision_state cfg;
	bool hit[COLLISION_CTX_COUNT];
	int32_t ax;
	int32_t ay;
	int32_t mx;
	int32_t my;
	uint32_t a25_sq;
	uint32_t vib_sq;
	size_t i;

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

	/* Yaw rate (robot frame, bias removed) summed over 250 ms. */
	i = n_samples % WIN_YAW;
	sum_yaw += s->gyro[2] - win_yaw[i];
	win_yaw[i] = s->gyro[2];

	n_samples++;

	/* Drive context. */
	if ((sides != prev_sides) && moving) {
		move_start_us = s->t_us;
	}
	prev_sides = sides;
	{
		bool driving = moving && commanded &&
			       ((uint32_t)(s->t_us - move_start_us) >= START_US);
		bool turning = driving && (d->turn != 0);

		hit[COLLISION_CTX_DRIVE] =
			driving && (turning ? (over(a25_sq, cfg.a25_turn_thr_mg) ||
					       over(vib_sq, cfg.vib_turn_thr_mg))
					    : (over(a25_sq, cfg.a25_thr_mg) ||
					       over(vib_sq, cfg.vib_thr_mg)));
	}

	/* IMU context: turning from the yaw rate alone, held after it drops. */
	{
		/* Counts per deg/s are 14.375; compare the 250 ms sum directly. */
		int32_t thr = (int32_t)((cfg.imu_turn_dps * IMU_GYRO_LSB_PER_DPS_X1000 * WIN_YAW) /
					1000U);
		bool turning;

		if ((sum_yaw > thr) || (sum_yaw < -thr)) {
			turn_seen_us = s->t_us;
			turn_seen = true;
		}
		turning = turn_seen && ((uint32_t)(s->t_us - turn_seen_us) <= TURN_HOLD_US);

		hit[COLLISION_CTX_IMU] =
			turning ? over(a25_sq, cfg.imu_a25_turn_thr_mg)
				: (over(a25_sq, cfg.imu_a25_thr_mg) ||
				   over(vib_sq, cfg.imu_vib_thr_mg));
	}

	for (i = 0U; i < COLLISION_CTX_COUNT; i++) {
		struct detector *dt = &det[i];

		if (dt->collecting) {
			if (dt->n < DIR_SAMPLES) {
				dt->dir_x += mx;
				dt->dir_y += my;
			}
			dt->a25_sq = MAX(dt->a25_sq, a25_sq);
			dt->vib_sq = MAX(dt->vib_sq, vib_sq);
			if (++dt->n == PEAK_SAMPLES) {
				dt->collecting = false;
				finish_event(i);
			}
			continue;
		}

		/* Elapsed time, not a deadline: t_us wraps every 71.6 minutes. */
		if (dt->holdoff && ((uint32_t)(s->t_us - dt->t_us) >= HOLDOFF_US)) {
			dt->holdoff = false;
		}

		if ((cfg.mode == COLLISION_OFF) || (n_samples < WARMUP) || !hit[i] ||
		    dt->holdoff) {
			continue;
		}

		/* Stop first; the direction and peaks follow over the next samples. */
		if ((cfg.mode == COLLISION_STOP) && (i == (size_t)cfg.acting)) {
			drive_inhibit_set(DRIVE_INHIBIT_COLLISION);
		}

		dt->collecting = true;
		dt->n = 0U;
		dt->t_us = s->t_us;
		dt->dir_x = 0;
		dt->dir_y = 0;
		dt->a25_sq = a25_sq;
		dt->vib_sq = vib_sq;
		dt->holdoff = true;
	}
}

void collision_set_mode(enum collision_mode mode)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.mode = mode;
	k_spin_unlock(&lock, key);
}

void collision_set_acting(enum collision_context ctx)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.acting = ctx;
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

void collision_set_imu_thresholds(uint16_t a25_mg, uint16_t vib_mg, uint16_t a25_turn_mg,
				  uint16_t turn_dps)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	state.imu_a25_thr_mg = a25_mg;
	state.imu_vib_thr_mg = vib_mg;
	state.imu_a25_turn_thr_mg = a25_turn_mg;
	state.imu_turn_dps = turn_dps;
	k_spin_unlock(&lock, key);
}

void collision_get(struct collision_state *st)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*st = state;
	k_spin_unlock(&lock, key);
}
