/*
 * "mind" shell command: runs the board bring-up tests on demand.
 *
 *   mind test_all   run every test and print a PASS/FAIL summary
 *   mind <part>     run a single test (plain "mind" lists them)
 *
 * Every test is a function returning 0 on success or a negative errno
 * value, printing its details through the shell it is called from.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "axisram_test.h"
#include "bme280_test.h"
#include "can_test.h"
#include "eeprom_test.h"
#include "i2c_scan.h"
#include "i2s_test.h"
#include "lan_test.h"
#include "pca9557_test.h"
#include "usb_device_test.h"
#include "usb_power_test.h"

struct mind_test {
	const char *name;
	int (*run)(const struct shell *sh);
};

/* Order used by "mind test_all": the tests that wait on something
 * external (DHCP server, USB host) run last.
 */
static const struct mind_test tests[] = {
	{ "axisram", axisram_test_run },
	{ "eeprom", eeprom_test_run },
	{ "i2c_scan", i2c_scan_test_run },
	{ "pca9557", pca9557_test_run },
	{ "bme280", bme280_test_run },
	{ "can", can_test_run },
	{ "usb_power", usb_power_test_run },
	{ "i2s", i2s_test_run },
	{ "lan", lan_test_run },
	{ "usb", usb_device_test_run },
};

static int run_test(const struct shell *sh, const struct mind_test *test)
{
	int ret;

	shell_print(sh, "--- %s ---", test->name);
	ret = test->run(sh);
	if (ret == 0) {
		shell_print(sh, "%s: PASS", test->name);
	} else {
		shell_error(sh, "%s: FAIL (%d)", test->name, ret);
	}

	return ret;
}

/* Shared handler of the single-test subcommands: argv[0] is the name of
 * the subcommand that was typed.
 */
static int cmd_single(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		if (strcmp(argv[0], tests[i].name) == 0) {
			return run_test(sh, &tests[i]);
		}
	}

	shell_error(sh, "unknown test '%s'", argv[0]);
	return -ENOENT;
}

static int cmd_test_all(const struct shell *sh, size_t argc, char **argv)
{
	int results[ARRAY_SIZE(tests)];
	size_t passed = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		results[i] = run_test(sh, &tests[i]);
		if (results[i] == 0) {
			passed++;
		}
		shell_print(sh, "");
	}

	shell_print(sh, "=== mind test_all summary ===");
	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		if (results[i] == 0) {
			shell_print(sh, "  %-10s PASS", tests[i].name);
		} else {
			shell_error(sh, "  %-10s FAIL (%d)", tests[i].name, results[i]);
		}
	}
	shell_print(sh, "%u/%u passed", (unsigned int)passed, (unsigned int)ARRAY_SIZE(tests));

	return (passed == ARRAY_SIZE(tests)) ? 0 : -EIO;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mind_cmds,
	SHELL_CMD(test_all, NULL, "Run every test below and print a PASS/FAIL summary",
		  cmd_test_all),
	SHELL_CMD(axisram, NULL, "AXISRAM3 write/read-back", cmd_single),
	SHELL_CMD(eeprom, NULL, "M24C64 EEPROM dump + non-destructive write test", cmd_single),
	SHELL_CMD(i2c_scan, NULL, "Scan both I2C buses for the expected devices", cmd_single),
	SHELL_CMD(pca9557, NULL, "PCA9557 GPIO expander walking-bit test", cmd_single),
	SHELL_CMD(bme280, NULL, "BME280 sample with plausibility check", cmd_single),
	SHELL_CMD(can, NULL, "MCP2515 CAN loopback frame", cmd_single),
	SHELL_CMD(usb_power, NULL, "MIC2026 USB 5V switch / fault status", cmd_single),
	SHELL_CMD(i2s, NULL, "MAX98357A test tone (2 s)", cmd_single),
	SHELL_CMD(lan, NULL, "Ethernet link + DHCPv4 address", cmd_single),
	SHELL_CMD(usb, NULL, "USB1 CDC-ACM enumeration by a host", cmd_single),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mind, &mind_cmds, "MindOS N6 board bring-up tests", NULL);
