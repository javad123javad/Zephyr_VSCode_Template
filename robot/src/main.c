/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "battery.h"
#include "drive.h"
#include "imu.h"
#include "motor.h"
#include "wifi.h"

LOG_MODULE_REGISTER(robot, LOG_LEVEL_INF);

/* Default run time of a drive command before the robot stops by itself. */
#define DRIVE_DEFAULT_MS 1000U

static int cmd_drive(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t ms = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10) : DRIVE_DEFAULT_MS;
	enum drive_move move;

	if (drive_parse(argv[0], strlen(argv[0]), &move) != 0) {
		return -EINVAL;
	}

	drive(move, ms);

	return 0;
}

static int cmd_motor(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long num = strtoul(argv[1], NULL, 10);
	int32_t speed;
	int err;

	if ((num < 1UL) || (num > (unsigned long)MOTOR_COUNT)) {
		shell_error(sh, "motor must be 1-%d", MOTOR_COUNT);
		return -EINVAL;
	}

	if (strcmp(argv[2], "fwd") == 0) {
		speed = MOTOR_SPEED_MAX;
	} else if (strcmp(argv[2], "rev") == 0) {
		speed = -MOTOR_SPEED_MAX;
	} else if (strcmp(argv[2], "stop") == 0) {
		speed = 0;
	} else {
		speed = (int32_t)strtol(argv[2], NULL, 10);
	}

	err = motor_set((enum motor_id)(num - 1UL), speed);
	if (err != 0) {
		shell_error(sh, "M%lu: failed (%d)", num, err);
	}

	return err;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const names[BATTERY_COUNT] = {"motor pack", "board cell"};
	struct battery_state st;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t i = 0U; i < BATTERY_COUNT; i++) {
		if (battery_get((enum battery_id)i, &st) != 0) {
			shell_print(sh, "%s: not measured", names[i]);
		} else {
			shell_print(sh, "%s: %u mV%s%s", names[i], st.mv, st.low ? " low" : "",
				    st.empty ? " empty" : "");
		}
	}

	shell_print(sh, "inhibit: 0x%x, idle: %u ms", drive_inhibit_get(), drive_idle_ms());

	return 0;
}

static int cmd_imu(const struct shell *sh, size_t argc, char **argv)
{
	struct imu_stats st;
	struct imu_sample s;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	imu_get(&st, &s);

	if (!st.accel_ok) {
		shell_print(sh, "IMU not running");
		return -ENODEV;
	}

	shell_print(sh, "%u Hz, %u samples, %u interrupt timeouts, %u dropped, %u corrupted, "
		    "gyro 0x%02x", st.rate_hz, st.samples, st.int_timeouts, st.dropped, st.rejected,
		    st.gyro_addr);
	shell_print(sh, "accel mg: %d %d %d", (s.accel[0] * IMU_ACCEL_UG_PER_LSB) / 1000,
		    (s.accel[1] * IMU_ACCEL_UG_PER_LSB) / 1000,
		    (s.accel[2] * IMU_ACCEL_UG_PER_LSB) / 1000);
	shell_print(sh, "gyro bias (sensor axes) mdeg/s: %d %d %d, measured %u times",
		    (st.gyro_bias_q4[0] * 1000000) / (16 * IMU_GYRO_LSB_PER_DPS_X1000),
		    (st.gyro_bias_q4[1] * 1000000) / (16 * IMU_GYRO_LSB_PER_DPS_X1000),
		    (st.gyro_bias_q4[2] * 1000000) / (16 * IMU_GYRO_LSB_PER_DPS_X1000),
		    st.bias_updates);
	shell_print(sh, "gyro deg/s x10: %d %d %d",
		    (s.gyro[0] * 10000) / IMU_GYRO_LSB_PER_DPS_X1000,
		    (s.gyro[1] * 10000) / IMU_GYRO_LSB_PER_DPS_X1000,
		    (s.gyro[2] * 10000) / IMU_GYRO_LSB_PER_DPS_X1000);

	return 0;
}

static int cmd_hang(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Stopping all threads; the watchdog should reset the robot");
	drive(DRIVE_STOP, 0U);
	k_msleep(100);

	/* Keep every other thread off the CPU, the watchdog's feeder included. */
	k_sched_lock();
	for (;;) {
	}

	return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	drive_inhibit_clear(DRIVE_INHIBIT_LATCHED);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(robot_cmds,
	SHELL_CMD_ARG(fwd, NULL, "Drive forward [ms, 0 = until stop]", cmd_drive, 1, 1),
	SHELL_CMD_ARG(back, NULL, "Drive backward [ms, 0 = until stop]", cmd_drive, 1, 1),
	SHELL_CMD_ARG(left, NULL, "Spin left [ms, 0 = until stop]", cmd_drive, 1, 1),
	SHELL_CMD_ARG(right, NULL, "Spin right [ms, 0 = until stop]", cmd_drive, 1, 1),
	SHELL_CMD(stop, NULL, "Stop all motors", cmd_drive),
	SHELL_CMD(status, NULL, "Battery and inhibit state", cmd_status),
	SHELL_CMD(imu, NULL, "IMU statistics and latest sample", cmd_imu),
	SHELL_CMD(reset, NULL, "Clear a latched inhibit (collision)", cmd_reset),
	SHELL_CMD(hang, NULL, "Hang on purpose to test the watchdog", cmd_hang),
	SHELL_CMD_ARG(motor, NULL, "Drive one motor: <1-4> <fwd|rev|stop|-100..100>", cmd_motor, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(robot, &robot_cmds, "4WD robot control", NULL);

int main(void)
{
	int err = motor_init();

	if (err != 0) {
		LOG_WRN("Some motors are unavailable (%d)", err);
	}

	err = battery_start();
	if ((err != 0) && (err != -ENOTSUP)) {
		LOG_ERR("Battery monitoring not started (%d)", err);
	}

	(void)imu_start();

	err = wifi_start();
	if (err != 0) {
		LOG_ERR("Wi-Fi not started (%d), shell control only", err);
	}

	LOG_INF("Robot ready");

	return 0;
}
