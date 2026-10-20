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
#include <zephyr/net/http/server.h>
#include <zephyr/shell/shell.h>

#include "battery.h"
#include "drive.h"
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
	SHELL_CMD(reset, NULL, "Clear a latched inhibit (collision)", cmd_reset),
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

	err = wifi_start();
	if (err != 0) {
		LOG_ERR("Wi-Fi not started (%d), shell control only", err);
	}

	err = http_server_start();
	if (err != 0) {
		LOG_ERR("Web server not started (%d)", err);
	}

	LOG_INF("Robot ready");

	return 0;
}
