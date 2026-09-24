/*
 * Scans both on-board I2C buses, prints a scan table (same format as
 * the "i2c scan" shell command) and checks that every device described
 * in the overlays answers at its devicetree address.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2c_scan.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* 7-bit address range scanned by the I2C shell's "i2c scan" command */
#define I2C_SCAN_ADDR_FIRST 0x04
#define I2C_SCAN_ADDR_LAST  0x77

struct i2c_expected {
	uint16_t addr;
	const char *name;
};

struct i2c_bus {
	const struct device *dev;
	const char *name;
	const struct i2c_expected *expected;
	size_t n_expected;
};

#define EXPECTED(_label, _name) { .addr = DT_REG_ADDR(DT_NODELABEL(_label)), .name = _name }

static const struct i2c_expected cnf_expected[] = {
	EXPECTED(board_eeprom, "M24C64 EEPROM"),
};

static const struct i2c_expected io_expected[] = {
	EXPECTED(io_expander, "PCA9557"),
	EXPECTED(bme280, "BME280"),
};

static const struct i2c_bus buses[] = {
	{ DEVICE_DT_GET(DT_NODELABEL(i2c1)), "i2c1 (io_cnf_i2c)", cnf_expected,
	  ARRAY_SIZE(cnf_expected) },
	{ DEVICE_DT_GET(DT_NODELABEL(i2c2)), "i2c2 (io_i2c)", io_expected,
	  ARRAY_SIZE(io_expected) },
};

static bool probe(const struct device *dev, uint16_t addr)
{
	struct i2c_msg msg;
	uint8_t dummy;

	msg.buf = &dummy;
	msg.len = 0U;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer(dev, &msg, 1, addr) == 0;
}

static int i2c_bus_scan(const struct shell *sh, const struct i2c_bus *bus)
{
	bool acked[I2C_SCAN_ADDR_LAST + 1] = { false };
	unsigned int found = 0;
	int ret = 0;

	if (!device_is_ready(bus->dev)) {
		shell_error(sh, "%s: device not ready", bus->name);
		return -ENODEV;
	}

	shell_print(sh, "%s:", bus->name);
	shell_print(sh, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

	for (uint16_t row = 0; row <= I2C_SCAN_ADDR_LAST; row += 16U) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x: ", row);
		for (uint16_t col = 0; col < 16U; col++) {
			uint16_t addr = row + col;

			if ((addr < I2C_SCAN_ADDR_FIRST) || (addr > I2C_SCAN_ADDR_LAST)) {
				shell_fprintf(sh, SHELL_NORMAL, "   ");
				continue;
			}

			acked[addr] = probe(bus->dev, addr);
			if (acked[addr]) {
				shell_fprintf(sh, SHELL_NORMAL, "%02x ", addr);
				found++;
			} else {
				shell_fprintf(sh, SHELL_NORMAL, "-- ");
			}
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	}

	shell_print(sh, "%s: %u device(s) found", bus->name, found);

	for (size_t i = 0; i < bus->n_expected; i++) {
		const struct i2c_expected *exp = &bus->expected[i];

		if (acked[exp->addr]) {
			shell_print(sh, "  0x%02x %s: present", exp->addr, exp->name);
		} else {
			shell_error(sh, "  0x%02x %s: MISSING", exp->addr, exp->name);
			ret = -ENODEV;
		}
	}

	return ret;
}

int i2c_scan_test_run(const struct shell *sh)
{
	int ret = 0;

	for (size_t i = 0; i < ARRAY_SIZE(buses); i++) {
		int err = i2c_bus_scan(sh, &buses[i]);

		if (err != 0) {
			ret = err;
		}
	}

	return ret;
}
