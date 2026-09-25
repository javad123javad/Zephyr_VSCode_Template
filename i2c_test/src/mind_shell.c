/*
 * "mind" shell command: runs the board bring-up tests on demand.
 *
 *   mind test_all   run every test and print a PASS/FAIL summary
 *   mind <part>     run a single test (plain "mind" lists them)
 *
 * Every test is a function returning 0 on success or a negative errno
 * value, printing its details through the shell it is called from. Tests of
 * parts that the plugged-in I/O card does not provide (per its EEPROM, read
 * by mind_io) are reported as SKIP instead of being run.
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

#include <mind_io/mind_io.h>

#define RESULT_SKIP 1

struct mind_test {
	const char *name;
	int (*run)(const struct shell *sh);
	/* NULL if the test applies to this card, else why it is skipped */
	const char *(*skip)(void);
};

static int card_test_run(const struct shell *sh)
{
	const struct mio_card *card = mio_card_get();
	const char *reason;
	enum mio_card_status st = mio_card_status(&reason);

	if (card == NULL) {
		shell_error(sh, "I/O card %s: %s (program it with mind_io/scripts/mio_card.py)",
			    mio_card_status_name(st), reason);
		return -ENODEV;
	}

	shell_print(sh, "I/O card '%s' rev %u.%u, serial '%s': %u I2C / %u SPI ports, %u lines",
		    card->identity.name, card->identity.hw_major, card->identity.hw_minor,
		    card->identity.serial, card->n_i2c_ports, card->n_spi_ports, card->n_lines);
	return 0;
}

static const char *needs_card_eeprom(void)
{
	return (mio_card_status(NULL) == MIO_CARD_ABSENT) ? "no I/O card EEPROM" : NULL;
}

static const char *needs_pca9557(void)
{
	const struct mio_card *card = mio_card_get();

	if (card == NULL) {
		return "no valid I/O card";
	}
	for (uint8_t i = 0U; i < card->n_devices; i++) {
		if (card->devices[i].type == MIO_DEV_PCA9557) {
			return NULL;
		}
	}
	return "the card has no on-card PCA9557";
}

static const char *needs_i2c_port0(void)
{
	return (mio_card_i2c_port(0) != NULL) ? NULL : "the card has no I2C port in slot 0";
}

static const char *needs_spi_port0(void)
{
	return (mio_card_spi_port(0) != NULL) ? NULL : "the card has no SPI port in slot 0";
}

static const char *needs_i2s(void)
{
	return mio_has_resource(MIO_RES_I2S) ? NULL : "the card does not use I2S";
}

static const char *needs_usb1(void)
{
	return mio_has_resource(MIO_RES_USB1) ? NULL : "the card does not route USB1";
}

/* Order used by "mind test_all": the tests that wait on something
 * external (DHCP server, USB host) run last.
 */
static const struct mind_test tests[] = {
	{ "card", card_test_run, NULL },
	{ "axisram", axisram_test_run, NULL },
	{ "eeprom", eeprom_test_run, needs_card_eeprom },
	{ "i2c_scan", i2c_scan_test_run, NULL },
	{ "pca9557", pca9557_test_run, needs_pca9557 },
	{ "bme280", bme280_test_run, needs_i2c_port0 },
	{ "can", can_test_run, needs_spi_port0 },
	{ "usb_power", usb_power_test_run, NULL },
	{ "i2s", i2s_test_run, needs_i2s },
	{ "lan", lan_test_run, NULL },
	{ "usb", usb_device_test_run, needs_usb1 },
};

/* Returns 0 (PASS), RESULT_SKIP or a negative errno value (FAIL) */
static int run_test(const struct shell *sh, const struct mind_test *test)
{
	const char *skip = (test->skip != NULL) ? test->skip() : NULL;
	int ret;

	shell_print(sh, "--- %s ---", test->name);
	if (skip != NULL) {
		shell_print(sh, "%s: SKIP (%s)", test->name, skip);
		return RESULT_SKIP;
	}

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
			int ret = run_test(sh, &tests[i]);

			return (ret == RESULT_SKIP) ? 0 : ret;
		}
	}

	shell_error(sh, "unknown test '%s'", argv[0]);
	return -ENOENT;
}

static int cmd_test_all(const struct shell *sh, size_t argc, char **argv)
{
	int results[ARRAY_SIZE(tests)];
	unsigned int passed = 0U;
	unsigned int skipped = 0U;
	unsigned int failed = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		results[i] = run_test(sh, &tests[i]);
		if (results[i] == 0) {
			passed++;
		} else if (results[i] == RESULT_SKIP) {
			skipped++;
		} else {
			failed++;
		}
		shell_print(sh, "");
	}

	shell_print(sh, "=== mind test_all summary ===");
	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		if (results[i] == 0) {
			shell_print(sh, "  %-10s PASS", tests[i].name);
		} else if (results[i] == RESULT_SKIP) {
			shell_print(sh, "  %-10s SKIP (%s)", tests[i].name, tests[i].skip());
		} else {
			shell_error(sh, "  %-10s FAIL (%d)", tests[i].name, results[i]);
		}
	}
	shell_print(sh, "%u passed, %u skipped, %u failed", passed, skipped, failed);

	return (failed == 0U) ? 0 : -EIO;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mind_cmds,
	SHELL_CMD(test_all, NULL, "Run every test below and print a PASS/SKIP/FAIL summary",
		  cmd_test_all),
	SHELL_CMD(card, NULL, "I/O card description read by mind_io", cmd_single),
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
